#include <WordTable.h>
#include <parse/parse_impl.h>
#include <utility>

#ifdef __GNUC__
#define LABEL_MAYBE_UNUSED [[maybe_unused]]
#define NO_INLINE [[gnu::noinline]]
#else
#define LABEL_MAYBE_UNUSED
#define NO_INLINE
#endif

using namespace std::string_view_literals;
enum class DeclarationKind : uint8_t {
    Namespace,
    Type,
    StaticValue,
    StaticObject,
    Function,
    Member,
    HasMember,
};

namespace parse {

static constexpr bool isWordBulkCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

static bool isWordFirstCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || c == '_' || c == '$' || c == '#';
}

static constexpr int_t SCOPE_BUFFER_SIZE = 1024;

struct ScopeBuffer {
    static size_t toIndex(ScopeKind* position) {
        return (uintptr_t)position & (SCOPE_BUFFER_SIZE - 1);
    }

    ScopeKind* buffer;
    ScopeBuffer()
        : buffer((ScopeKind*)::operator new(SCOPE_BUFFER_SIZE, std::align_val_t(SCOPE_BUFFER_SIZE))) { }
    ~ScopeBuffer() {
        ::operator delete(buffer, SCOPE_BUFFER_SIZE, std::align_val_t(SCOPE_BUFFER_SIZE));
    }
};

static ScopeKind* pushScope(ScopeKind* position, ScopeKind kind) {
    // fmt::println("pushScope {}", nameString(kind));
    auto index = ScopeBuffer::toIndex(position);
    VERIFY(index + 1 < (size_t)SCOPE_BUFFER_SIZE);
    position += 1;
    position[0] = kind;
    return position;
}

template<typename... Args>
static ScopeKind* popScope(ScopeKind* position, Args... kinds) {
    // fmt::println("popScope {}", nameString(*position));
    static_assert((std::is_same_v<Args, ScopeKind> && ...));
    if (((position[0] != kinds) && ...))
        return nullptr;
    position -= 1;
    return position;
}

static constexpr int_t ARGUMENT_BUFFER_SIZE = 1024;
static constexpr int_t ARGUMENT_BUFFER_SIZE_IN_BYTES = ARGUMENT_BUFFER_SIZE * sizeof(Word);

struct ArgumentBuffer {
    static size_t toIndex(Word* position) {
        return ((uintptr_t)position & (ARGUMENT_BUFFER_SIZE_IN_BYTES - 1)) / sizeof(Word);
    }
    Word* buffer;
    ArgumentBuffer()
        : buffer((Word*)::operator new(ARGUMENT_BUFFER_SIZE_IN_BYTES, std::align_val_t(ARGUMENT_BUFFER_SIZE_IN_BYTES))) { }
    ~ArgumentBuffer() {
        ::operator delete(buffer, ARGUMENT_BUFFER_SIZE_IN_BYTES, std::align_val_t(ARGUMENT_BUFFER_SIZE_IN_BYTES));
    }
};

static Word* addCallArgument(Word* position, Word name) {
    auto index = ArgumentBuffer::toIndex(position);
    VERIFY(index + 1 < (size_t)ARGUMENT_BUFFER_SIZE);
    uint32_t newCount = position[0].toUint() + 1;
    position[0] = name;
    position += 1;
    position[0] = Word::fromUint(newCount);
    return position;
}

NO_INLINE static Word* endCall(Word* position, ParseState& state) {
    uint32_t count = position[0].toUint();
    auto& outputArgs = state.parseOutput.callArguments;
    uint32_t outputPos = outputArgs.size();
    outputArgs.push_back(position[0]);
    outputArgs.insert(outputArgs.end(), position - count, position);
    position -= count + 2;
    state.parseOutput.tokens[position[1].toUint()].dataBits = outputPos;
    return position;
}

static SourceLocation locationInCurrentLine(const char* position, ParseState& state) {
    return { (uint32_t)(position - state.parseOutput.lines.back().begin), (uint32_t)state.parseOutput.lines.size() - 1 };
}

NO_INLINE static void emitToken(TokenKind kind, const char* begin, uint32_t data, ParseState& state) {
    if (kind == TokenKind::ImplicitKindParameter) {
        VERIFY(data != 0);
    }
    state.parseOutput.tokens.push_back({ kind, locationInCurrentLine(begin, state), data });
}

NO_INLINE static Word* emitCallToken(Word* argPos, TokenKind kind, const char* begin, ParseState& state) {
    uint32_t tokenIndex = state.parseOutput.tokens.size();
    state.parseOutput.tokens.push_back({ kind, locationInCurrentLine(begin, state), 0 });

    auto index = ArgumentBuffer::toIndex(argPos);
    VERIFY(index + 2 < (size_t)ARGUMENT_BUFFER_SIZE);
    argPos[1] = Word::fromUint(tokenIndex);
    argPos[2] = Word::fromUint(0);
    argPos += 2;
    return argPos;
}

NO_INLINE static void markLineBegin(const char* position, ParseState& state) {
    state.parseOutput.lines.push_back({ position });
}

struct WordAndPosition {
    const char* position;
    Word word;
};
[[nodiscard]] NO_INLINE static WordAndPosition readWord(const char* position, ParseState& state) {
    const char* wordBegin = position;
    Word::HashState hashState;
    do {
        Word::iterateHash(hashState, position[0]);
        position += 1;
    } while (isWordBulkCharacter(position[0]));
    auto hash = Word::finalizeHash(hashState);
    Word word = state.wordTable.getWithHash(std::string_view(wordBegin, position), hash);
    return { position, word };
}

[[nodiscard]] static const char* skipWhitespace(const char* position) {
    while (position[0] == ' ' || position[0] == '\t')
        position += 1;
    return position;
}

// advances offset to the next '*/'
[[nodiscard]] static const char* skipToEndOfBlockComment(const char* position) {
    while (position[0] != '\0' && !(position[0] == '*' && position[1] == '/')) {
        position += 1;
    }
    return position;
};

// advances offset to the next new line character
[[nodiscard]] static const char* skipToEndOfLine(const char* position) {
    while (position[0] != '\0' && position[0] != '\n' && position[0] != '\r') {
        position += 1;
    }
    return position;
};

[[nodiscard]] static const char* skipToEndOfCharacterLiteral(const char* position) {
    position += 1;
    while (position[0] != '\0' && position[0] != '\'' && position[0] != '\n' && position[0] != '\r') {
        position += 1;
    }
    return position;
};

NO_INLINE static void emitWhitespace(WhitespaceKind kind, const char* begin, const char* end, ParseState& state) {
    state.parseOutput.whitespace.push_back({ { kind, locationInCurrentLine(begin, state) }, (uint32_t)(end - begin) });
}

[[nodiscard]] NO_INLINE static const char* inlineAdvancer(const char* tokEnd, ParseState& state) {
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        const char* tokBegin = tokEnd;
        if (std::string_view(tokEnd, 2) == "//") {
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, state);
            continue;
        }
        if (std::string_view(tokEnd, 2) == "/*") {
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, state);
            continue;
        }
        if (tokEnd[0] == '\n') {
            tokEnd += 1;
            markLineBegin(tokEnd, state);
            continue;
        }
        if (tokEnd[0] == '\r') {
            if (tokEnd[1] == '\n') {
                tokEnd += 2;
            } else {
                tokEnd += 1;
            }
            markLineBegin(tokEnd, state);
            continue;
        }
        break;
    }
    return tokEnd;
}

template<DeclarationKind kind>
static sema::ScopeValue commitDeclaration(Word name, const char* currentPosition, TokenHandle declarationBegin, ParseState& state) {
    // fmt::println("commitDeclaration {}", state.wordTable.view(name));
    if constexpr (kind == DeclarationKind::Member || kind == DeclarationKind::HasMember) {
        return state.pushMemberScope(name, declarationBegin, locationInCurrentLine(currentPosition, state));
    } else if constexpr (kind == DeclarationKind::Namespace) {
        return state.pushNamespaceScope(name);
    } else {
        sema::ProgramKind progKind;
        switch (kind) {
        case DeclarationKind::Type:
            progKind = sema::ProgramKind::Type;
            break;
        case DeclarationKind::Function:
            progKind = sema::ProgramKind::Function;
            break;
        case DeclarationKind::StaticValue:
            progKind = sema::ProgramKind::Value;
            break;
        case DeclarationKind::StaticObject:
            progKind = sema::ProgramKind::Object;
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        return state.pushStaticScope(progKind, name, declarationBegin, locationInCurrentLine(currentPosition, state));
    }
}

static void endDeclaration(ParseState& state) {
    // fmt::println("endDeclaration {}", state.wordTable.view(state.currentScope()->name()));
    state.popScope();
}

void parseImpl(const char* sourceBufferPosition, ParseState& state, ErrorHandler* errorHandler) {
    ScopeBuffer scopeBuffer;
    ScopeKind* scopePosition = scopeBuffer.buffer;
    scopePosition[0] = ScopeKind::Invalid;
    ArgumentBuffer argumentBuffer;
    Word* argumentPosition = argumentBuffer.buffer;

    const char* tokBegin = sourceBufferPosition;
    const char* tokEnd = sourceBufferPosition;
    TokenKind carriedEmitTokenKind = (TokenKind)0;
    uint32_t carriedEmitTokenData = 0;
    Word this_identifier;
    sema::ScopeValue this_declaration;
    TokenHandle declarationBegin = {};
    Word argumentName;

    TokenKind tokenKind = (TokenKind)0;

    scopePosition = pushScope(scopePosition, ScopeKind::Namespace);
    State parseState = State::NamespaceDeclaration;
    LexerToken errorToken = (LexerToken)0;

    switch (parseState) {
    case State::Expression:
        goto expression$no_emit;
    case State::AfterExpression:
        goto after_expression$no_emit;
    case State::CommaAfterExpression:
        goto comma_after_expression$no_emit;
    case State::CommaElse:
        goto comma_else$no_emit;
    case State::CheckDesignatedArgument:
        VERIFY_NOT_REACHED();
    case State::MaybeDesignatedArgument:
        goto maybe_designated_argument$no_emit;
    case State::FirstArgumentParen:
        goto first_argument_paren$no_emit;
    case State::FirstArgumentSquare:
        goto first_argument_square$no_emit;
    case State::FirstArgumentBrace:
        goto first_argument_brace$no_emit;
    case State::AccessPunctuation:
        goto access_punctuation$no_emit;
    case State::SingleOrCompoundStatement:
        goto single_or_compound_statement$no_emit;
    case State::AfterStatement:
        goto after_statement$no_emit;
    case State::Statement:
        goto statement$no_emit;
    case State::AfterReturn:
        goto after_return$no_emit;
    case State::ElseBranch:
        goto else_branch$no_emit;
    case State::CheckVarAfterLet:
        goto check_var_after_let$no_emit;
    case State::VariableDeclaration:
        goto variable_declaration$no_emit;
    case State::AfterVariableDeclarationId:
        goto after_variable_declaration_id$no_emit;
    case State::AfterParameters:
        goto after_parameters$no_emit;
    case State::FirstParameter:
        goto first_parameter$no_emit;
    case State::Parameter:
        goto parameter$no_emit;
    case State::NoDeclaration:
        VERIFY_NOT_REACHED();
    case State::NamespaceDeclaration:
        goto namespace_declaration$no_emit;
    case State::NamespaceDeclarationId:
        goto namespace_declaration_id$no_emit;
    case State::AfterNamespaceDeclarationId:
        goto after_namespace_declaration_id$no_emit;
    case State::NamespaceDeclarationBody:
        goto namespace_declaration_body$no_emit;
    case State::TemplatedDeclaration:
        VERIFY_NOT_REACHED();
    case State::TemplatedDeclarationWithAttributes:
        goto templated_declaration_with_attributes$no_emit;
    case State::AfterTemplate:
        goto after_template$no_emit;
    case State::AfterTemplateParameters:
        VERIFY_NOT_REACHED();
    case State::FunctionDeclarationId:
        goto function_declaration_id$no_emit;
    case State::AfterFunctionDeclarationId:
        goto after_function_declaration_id$no_emit;
    case State::AfterFunctionParameters:
        VERIFY_NOT_REACHED();
    case State::TypeDeclarationId:
        goto type_declaration_id$no_emit;
    case State::AfterTypeDeclarationId:
        goto after_type_declaration_id$no_emit;
    case State::TypeDeclarationBody:
        goto type_declaration_body$no_emit;
    case State::MemberDeclaration:
        goto member_declaration$no_emit;
    case State::AfterStatic:
        goto after_static$no_emit;
    case State::StaticLetVariableDeclaration:
        goto static_let_variable_declaration$no_emit;
    case State::StaticVarVariableDeclaration:
        goto static_var_variable_declaration$no_emit;
    case State::AfterDeclaration:
        goto after_declaration$no_emit;
    case State::Error:
        VERIFY_NOT_REACHED();
    }
    // SwitchState expression
expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
expression$no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::Expression;
expression$as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, state);
        goto expression$no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, state);
        goto expression$no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::ExclaimEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitToken TokenKind::LogicalNotExpr
        carriedEmitTokenKind = TokenKind::LogicalNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::PercentEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Percent;
        goto handle_parse_error;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = LexerToken::AmpAmpEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::AmpAmp;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::AmpEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Amp;
        goto handle_parse_error;
    }
    case '(': {
        tokEnd += 1;
        // emitCallToken TokenKind::ParenthesizedExpr
        argumentPosition = emitCallToken(argumentPosition, TokenKind::ParenthesizedExpr, tokBegin, state);
        // next first_argument_paren
        goto first_argument_paren$no_emit;
    }
    case ')': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::RightParen;
        goto handle_parse_error;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::StarEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitToken TokenKind::DereferenceExpr
        carriedEmitTokenKind = TokenKind::DereferenceExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitToken TokenKind::PreIncrementExpr
            carriedEmitTokenKind = TokenKind::PreIncrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::PlusEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitToken TokenKind::PlusExpr
        carriedEmitTokenKind = TokenKind::PlusExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ',': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Comma;
        goto handle_parse_error;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // emitToken TokenKind::PreDecrementExpr
            carriedEmitTokenKind = TokenKind::PreDecrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::MinusEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::MinusGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitToken TokenKind::NegateExpr
        carriedEmitTokenKind = TokenKind::NegateExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Point;
        goto handle_parse_error;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, state);
            goto expression$no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, state);
            goto expression$no_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::SlashEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Slash;
        goto handle_parse_error;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::ColonColon;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Colon;
        goto handle_parse_error;
    }
    case ';': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::SemiColon;
        goto handle_parse_error;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = LexerToken::LessLessEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::LessLess;
            goto handle_parse_error;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = LexerToken::LessEqualGreater;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::LessEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Less;
        goto handle_parse_error;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::EqualEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::EqualGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Equal;
        goto handle_parse_error;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::GreaterEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = LexerToken::GreaterGreaterEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::GreaterGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Greater;
        goto handle_parse_error;
    }
    case '[': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::LeftSqure;
        goto handle_parse_error;
    }
    case ']': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::RightSqure;
        goto handle_parse_error;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::HatEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Hat;
        goto handle_parse_error;
    }
    case '{': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::LeftBrace;
        goto handle_parse_error;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::VertEqual;
            goto handle_parse_error;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = LexerToken::VertVertEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::VertVert;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Vert;
        goto handle_parse_error;
    }
    case '}': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::RightBrace;
        goto handle_parse_error;
    }
    case '~': {
        tokEnd += 1;
        // emitToken TokenKind::BitwiseNotExpr
        carriedEmitTokenKind = TokenKind::BitwiseNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        do {
            tokEnd += 1;
        } while (tokEnd[0] >= '0' && tokEnd[0] <= '9');
        // emitToken TokenKind::LiteralExpr
        carriedEmitTokenKind = TokenKind::LiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // emitToken TokenKind::LiteralExpr
        carriedEmitTokenKind = TokenKind::LiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '#':
    case '$':
    case '_':
        goto expression$word_case;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
expression$word_case:
    {
        auto wordAndPos = readWord(tokEnd, state);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (this_identifier.keyword()) {
    LABEL_MAYBE_UNUSED expression$keyword_check:
        if (this_identifier == words["if"]) {
            // pushScope ScopeKind::IfExpr
            scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
            // next expression
            goto expression$no_emit;
        }
        // -> error
        goto error$keyword_check;
    }
    // emitToken TokenKind::IdentifierExpr, this_identifier
    carriedEmitTokenKind = TokenKind::IdentifierExpr;
    carriedEmitTokenData = this_identifier.toUint();
    // next after_expression
    goto after_expression$with_emit;

    // SwitchState after_expression
after_expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_expression$no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::AfterExpression;
after_expression$as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, state);
        goto after_expression$no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, state);
        goto after_expression$no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareNotEqualExpr
            carriedEmitTokenKind = TokenKind::CompareNotEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Exclaim;
        goto handle_parse_error;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::PercentEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::RemainderUpdateStmt
            carriedEmitTokenKind = TokenKind::RemainderUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::RemainderExpr
        carriedEmitTokenKind = TokenKind::RemainderExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // popScope ScopeKind::LeftExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                    if (result == nullptr) {
                        errorToken = LexerToken::AmpAmpEqual;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::LogicalAndUpdateStmt
                carriedEmitTokenKind = TokenKind::LogicalAndUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::LogicalAndExpr
            carriedEmitTokenKind = TokenKind::LogicalAndExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::AmpEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::BitwiseAndUpdateStmt
            carriedEmitTokenKind = TokenKind::BitwiseAndUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseAndExpr
        carriedEmitTokenKind = TokenKind::BitwiseAndExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '(': {
        tokEnd += 1;
        // emitCallToken TokenKind::CallExpr
        argumentPosition = emitCallToken(argumentPosition, TokenKind::CallExpr, tokBegin, state);
        // next first_argument_paren
        goto first_argument_paren$no_emit;
    }
    case ')': {
        tokEnd += 1;
        // ifScope ScopeKind::RightExpr
        if (scopePosition[0] == ScopeKind::RightExpr) {
            // popScope ScopeKind::RightExpr
            {
                auto result = popScope(scopePosition, ScopeKind::RightExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    errorToken = LexerToken::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::ExpressionStmt
            carriedEmitTokenKind = TokenKind::ExpressionStmt;
            carriedEmitTokenData = 0;
            // next after_parameters
            goto after_parameters$with_emit;
        }
        // ifScope ScopeKind::VariableType
        if (scopePosition[0] == ScopeKind::VariableType) {
            // popScope ScopeKind::VariableType
            {
                auto result = popScope(scopePosition, ScopeKind::VariableType);
                if (result == nullptr) {
                    errorToken = LexerToken::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    errorToken = LexerToken::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::AssignStmt
            emitToken(TokenKind::AssignStmt, tokBegin, 0, state);
            // emitToken TokenKind::ExpressionStmt
            carriedEmitTokenKind = TokenKind::ExpressionStmt;
            carriedEmitTokenData = 0;
            // next after_parameters
            goto after_parameters$with_emit;
        }
        // popScope ScopeKind::Paren
        {
            auto result = popScope(scopePosition, ScopeKind::Paren);
            if (result == nullptr) {
                errorToken = LexerToken::RightParen;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::StarEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::MultiplyUpdateStmt
            carriedEmitTokenKind = TokenKind::MultiplyUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::MultiplyExpr
        carriedEmitTokenKind = TokenKind::MultiplyExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitToken TokenKind::PostIncrementExpr
            carriedEmitTokenKind = TokenKind::PostIncrementExpr;
            carriedEmitTokenData = 0;
            // next after_expression
            goto after_expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::PlusEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AdditionUpdateStmt
            carriedEmitTokenKind = TokenKind::AdditionUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::AdditionExpr
        carriedEmitTokenKind = TokenKind::AdditionExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ',': {
        tokEnd += 1;
        // next comma_after_expression
        goto comma_after_expression$no_emit;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // emitToken TokenKind::PostDecrementExpr
            carriedEmitTokenKind = TokenKind::PostDecrementExpr;
            carriedEmitTokenData = 0;
            // next after_expression
            goto after_expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::MinusEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::SubtractionUpdateStmt
            carriedEmitTokenKind = TokenKind::SubtractionUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = LexerToken::MinusGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitToken TokenKind::SubtractionExpr
        carriedEmitTokenKind = TokenKind::SubtractionExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // tokenKind = TokenKind::MemberAccessExpr
        tokenKind = TokenKind::MemberAccessExpr;
        // next access_punctuation
        goto access_punctuation$no_emit;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, state);
            goto after_expression$no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, state);
            goto after_expression$no_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::SlashEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::DivideUpdateStmt
            carriedEmitTokenKind = TokenKind::DivideUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::DivideExpr
        carriedEmitTokenKind = TokenKind::DivideExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // tokenKind = TokenKind::StaticAccessExpr
            tokenKind = TokenKind::StaticAccessExpr;
            // next access_punctuation
            goto access_punctuation$no_emit;
        }
        tokEnd += 1;
        // ifScope ScopeKind::HasTypeExpr
        if (scopePosition[0] == ScopeKind::HasTypeExpr) {
            // popScope ScopeKind::HasTypeExpr
            {
                auto result = popScope(scopePosition, ScopeKind::HasTypeExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::Colon;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // next type_declaration_body
            goto type_declaration_body$no_emit;
        }
        // ifScope ScopeKind::ReturnType
        if (scopePosition[0] == ScopeKind::ReturnType) {
            // popScope ScopeKind::ReturnType
            {
                auto result = popScope(scopePosition, ScopeKind::ReturnType);
                if (result == nullptr) {
                    errorToken = LexerToken::Colon;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::FunctionBody
            scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
            // emitToken TokenKind::FunctionBody
            carriedEmitTokenKind = TokenKind::FunctionBody;
            carriedEmitTokenData = 0;
            // next single_or_compound_statement
            goto single_or_compound_statement$with_emit;
        }
        // popScope ScopeKind::IfExprOrStmt
        {
            auto result = popScope(scopePosition, ScopeKind::IfExprOrStmt);
            if (result == nullptr) {
                errorToken = LexerToken::Colon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::LeftExpr
        {
            auto result = popScope(scopePosition, ScopeKind::LeftExpr);
            if (result == nullptr) {
                errorToken = LexerToken::Colon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::PlainStatement
        {
            auto result = popScope(scopePosition, ScopeKind::PlainStatement);
            if (result == nullptr) {
                errorToken = LexerToken::Colon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::IfBranch
        scopePosition = pushScope(scopePosition, ScopeKind::IfBranch);
        // emitToken TokenKind::IfStmt
        carriedEmitTokenKind = TokenKind::IfStmt;
        carriedEmitTokenData = 0;
        // next single_or_compound_statement
        goto single_or_compound_statement$with_emit;
    }
    case ';': {
        tokEnd += 1;
        // ifScope ScopeKind::HasTypeExpr
        if (scopePosition[0] == ScopeKind::HasTypeExpr) {
            // popScope ScopeKind::HasTypeExpr
            {
                auto result = popScope(scopePosition, ScopeKind::HasTypeExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::SemiColon;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::EmptyNode
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next after_declaration
            goto after_declaration$with_emit;
        }
        // ifScope ScopeKind::VariableType
        if (scopePosition[0] == ScopeKind::VariableType) {
            // popScope ScopeKind::VariableType
            {
                auto result = popScope(scopePosition, ScopeKind::VariableType);
                if (result == nullptr) {
                    errorToken = LexerToken::SemiColon;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AssignStmt
            emitToken(TokenKind::AssignStmt, tokBegin, 0, state);
        }
        // popScope ScopeKind::LeftExpr, ScopeKind::RightExpr
        {
            auto result = popScope(scopePosition, ScopeKind::LeftExpr, ScopeKind::RightExpr);
            if (result == nullptr) {
                errorToken = LexerToken::SemiColon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitToken TokenKind::ExpressionStmt
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_statement
        goto after_statement$with_emit;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // popScope ScopeKind::LeftExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                    if (result == nullptr) {
                        errorToken = LexerToken::LessLessEqual;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::ShiftLeftUpdateStmt
                carriedEmitTokenKind = TokenKind::ShiftLeftUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::ShiftLeftExpr
            carriedEmitTokenKind = TokenKind::ShiftLeftExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = LexerToken::LessEqualGreater;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // emitToken TokenKind::CompareLessEqualExpr
            carriedEmitTokenKind = TokenKind::CompareLessEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::CompareLessExpr
        carriedEmitTokenKind = TokenKind::CompareLessExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareEqualExpr
            carriedEmitTokenKind = TokenKind::CompareEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '>') {
            tokEnd += 2;
            // popScope ScopeKind::IfExpr, ScopeKind::IfExprOrStmt
            {
                auto result = popScope(scopePosition, ScopeKind::IfExpr, ScopeKind::IfExprOrStmt);
                if (result == nullptr) {
                    errorToken = LexerToken::EqualGreater;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::IfExpr
            carriedEmitTokenKind = TokenKind::IfExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // popScope ScopeKind::VariableType, ScopeKind::LeftExpr
        {
            auto result = popScope(scopePosition, ScopeKind::VariableType, ScopeKind::LeftExpr);
            if (result == nullptr) {
                errorToken = LexerToken::Equal;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // emitToken TokenKind::AssignStmt
        carriedEmitTokenKind = TokenKind::AssignStmt;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareGreaterEqualExpr
            carriedEmitTokenKind = TokenKind::CompareGreaterEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // popScope ScopeKind::LeftExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                    if (result == nullptr) {
                        errorToken = LexerToken::GreaterGreaterEqual;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::ShiftRightUpdateStmt
                carriedEmitTokenKind = TokenKind::ShiftRightUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::ShiftRightExpr
            carriedEmitTokenKind = TokenKind::ShiftRightExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::CompareGreaterExpr
        carriedEmitTokenKind = TokenKind::CompareGreaterExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '[': {
        tokEnd += 1;
        // emitCallToken TokenKind::IndexExpr
        argumentPosition = emitCallToken(argumentPosition, TokenKind::IndexExpr, tokBegin, state);
        // next first_argument_square
        goto first_argument_square$no_emit;
    }
    case ']': {
        tokEnd += 1;
        // popScope ScopeKind::Square
        {
            auto result = popScope(scopePosition, ScopeKind::Square);
            if (result == nullptr) {
                errorToken = LexerToken::RightSqure;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::HatEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::BitwiseXorUpdateStmt
            carriedEmitTokenKind = TokenKind::BitwiseXorUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseXorExpr
        carriedEmitTokenKind = TokenKind::BitwiseXorExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '{': {
        tokEnd += 1;
        // emitCallToken TokenKind::Parameterize
        argumentPosition = emitCallToken(argumentPosition, TokenKind::Parameterize, tokBegin, state);
        // next first_argument_brace
        goto first_argument_brace$no_emit;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::VertEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::BitwiseOrUpdateStmt
            carriedEmitTokenKind = TokenKind::BitwiseOrUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // popScope ScopeKind::LeftExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                    if (result == nullptr) {
                        errorToken = LexerToken::VertVertEqual;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::LogicalOrUpdateStmt
                carriedEmitTokenKind = TokenKind::LogicalOrUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::LogicalOrExpr
            carriedEmitTokenKind = TokenKind::LogicalOrExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseOrExpr
        carriedEmitTokenKind = TokenKind::BitwiseOrExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '}': {
        tokEnd += 1;
        // popScope ScopeKind::Brace
        {
            auto result = popScope(scopePosition, ScopeKind::Brace);
            if (result == nullptr) {
                errorToken = LexerToken::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '~': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Tilde;
        goto handle_parse_error;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        do {
            tokEnd += 1;
        } while (tokEnd[0] >= '0' && tokEnd[0] <= '9');
        // -> error
        // error
        errorToken = LexerToken::Literal;
        goto handle_parse_error;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // -> error
        // error
        errorToken = LexerToken::Literal;
        goto handle_parse_error;
    }
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '#':
    case '$':
    case '_':
        goto after_expression$word_case;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
after_expression$word_case:
    {
        auto wordAndPos = readWord(tokEnd, state);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (this_identifier.keyword()) {
        // -> error
        goto error$keyword_check;
    }
    // -> error
    // error
    errorToken = LexerToken::Identifier;
    goto handle_parse_error;

    // LinearState comma_after_expression
comma_after_expression$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::CommaAfterExpression;
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Paren
        {
            auto result = popScope(scopePosition, ScopeKind::Paren);
            if (result == nullptr) {
                errorToken = LexerToken::RightParen;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (std::string_view(tokEnd, 1) == "]"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Square
        {
            auto result = popScope(scopePosition, ScopeKind::Square);
            if (result == nullptr) {
                errorToken = LexerToken::RightSqure;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Brace
        {
            auto result = popScope(scopePosition, ScopeKind::Brace);
            if (result == nullptr) {
                errorToken = LexerToken::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED comma_after_expression$keyword_check:
            if (this_identifier == words["else"]) {
                // next comma_else
                goto comma_else$no_emit;
            }
            // ifScope ScopeKind::Parameter
            if (scopePosition[0] == ScopeKind::Parameter) {
                // then parameter
                goto parameter$keyword_check;
            }
            // ifScope ScopeKind::VariableType
            if (scopePosition[0] == ScopeKind::VariableType) {
                // popScope ScopeKind::VariableType
                {
                    auto result = popScope(scopePosition, ScopeKind::VariableType);
                    if (result == nullptr) {
                        goto error$as_then;
                    }
                    scopePosition = result;
                }
                // popScope ScopeKind::Parameter
                {
                    auto result = popScope(scopePosition, ScopeKind::Parameter);
                    if (result == nullptr) {
                        goto error$as_then;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::Parameter
                scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
                // emitToken TokenKind::AssignStmt
                emitToken(TokenKind::AssignStmt, tokBegin, 0, state);
                // emitToken TokenKind::ExpressionStmt
                emitToken(TokenKind::ExpressionStmt, tokBegin, 0, state);
                // then parameter
                goto parameter$keyword_check;
            }
            // ifScope ScopeKind::RightExpr
            if (scopePosition[0] == ScopeKind::RightExpr) {
                // popScope ScopeKind::RightExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::RightExpr);
                    if (result == nullptr) {
                        goto error$as_then;
                    }
                    scopePosition = result;
                }
                // popScope ScopeKind::Parameter
                {
                    auto result = popScope(scopePosition, ScopeKind::Parameter);
                    if (result == nullptr) {
                        goto error$as_then;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::Parameter
                scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
                // emitToken TokenKind::ExpressionStmt
                emitToken(TokenKind::ExpressionStmt, tokBegin, 0, state);
                // then parameter
                goto parameter$keyword_check;
            }
            // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace
            if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace) {
                // then check_designated_argument
                // callArgument
                argumentPosition = addCallArgument(argumentPosition, Word());
                // emitToken TokenKind::CallArgument
                emitToken(TokenKind::CallArgument, tokBegin, 0, state);
                // -> expression
                goto expression$keyword_check;
            }
            // -> error
            goto error$keyword_check;
        }
        // ifScope ScopeKind::Parameter
        if (scopePosition[0] == ScopeKind::Parameter) {
            // then parameter
            // tokenKind = TokenKind::ImplicitKindParameter
            tokenKind = TokenKind::ImplicitKindParameter;
            // -> variable_declaration
            // emitToken tokenKind, this_identifier
            carriedEmitTokenKind = tokenKind;
            carriedEmitTokenData = this_identifier.toUint();
            // next after_variable_declaration_id
            goto after_variable_declaration_id$with_emit;
        }
        // ifScope ScopeKind::VariableType
        if (scopePosition[0] == ScopeKind::VariableType) {
            // popScope ScopeKind::VariableType
            {
                auto result = popScope(scopePosition, ScopeKind::VariableType);
                if (result == nullptr) {
                    goto error$as_then;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    goto error$as_then;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::Parameter
            scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
            // emitToken TokenKind::AssignStmt
            emitToken(TokenKind::AssignStmt, tokBegin, 0, state);
            // emitToken TokenKind::ExpressionStmt
            emitToken(TokenKind::ExpressionStmt, tokBegin, 0, state);
            // then parameter
            // tokenKind = TokenKind::ImplicitKindParameter
            tokenKind = TokenKind::ImplicitKindParameter;
            // -> variable_declaration
            // emitToken tokenKind, this_identifier
            carriedEmitTokenKind = tokenKind;
            carriedEmitTokenData = this_identifier.toUint();
            // next after_variable_declaration_id
            goto after_variable_declaration_id$with_emit;
        }
        // ifScope ScopeKind::RightExpr
        if (scopePosition[0] == ScopeKind::RightExpr) {
            // popScope ScopeKind::RightExpr
            {
                auto result = popScope(scopePosition, ScopeKind::RightExpr);
                if (result == nullptr) {
                    goto error$as_then;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    goto error$as_then;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::Parameter
            scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
            // emitToken TokenKind::ExpressionStmt
            emitToken(TokenKind::ExpressionStmt, tokBegin, 0, state);
            // then parameter
            // tokenKind = TokenKind::ImplicitKindParameter
            tokenKind = TokenKind::ImplicitKindParameter;
            // -> variable_declaration
            // emitToken tokenKind, this_identifier
            carriedEmitTokenKind = tokenKind;
            carriedEmitTokenData = this_identifier.toUint();
            // next after_variable_declaration_id
            goto after_variable_declaration_id$with_emit;
        }
        // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace
        if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace) {
            // then check_designated_argument
            // argumentName = this_identifier
            argumentName = this_identifier;
            // next maybe_designated_argument
            goto maybe_designated_argument$no_emit;
        }
        // -> error
        // error
        errorToken = LexerToken::Identifier;
        goto handle_parse_error;
    }
    // ifScope ScopeKind::Parameter
    if (scopePosition[0] == ScopeKind::Parameter) {
        // then parameter
        goto parameter$as_then;
    }
    // ifScope ScopeKind::VariableType
    if (scopePosition[0] == ScopeKind::VariableType) {
        // popScope ScopeKind::VariableType
        {
            auto result = popScope(scopePosition, ScopeKind::VariableType);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::Parameter
        scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
        // emitToken TokenKind::AssignStmt
        emitToken(TokenKind::AssignStmt, tokBegin, 0, state);
        // emitToken TokenKind::ExpressionStmt
        emitToken(TokenKind::ExpressionStmt, tokBegin, 0, state);
        // then parameter
        goto parameter$as_then;
    }
    // ifScope ScopeKind::RightExpr
    if (scopePosition[0] == ScopeKind::RightExpr) {
        // popScope ScopeKind::RightExpr
        {
            auto result = popScope(scopePosition, ScopeKind::RightExpr);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::Parameter
        scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
        // emitToken TokenKind::ExpressionStmt
        emitToken(TokenKind::ExpressionStmt, tokBegin, 0, state);
        // then parameter
        goto parameter$as_then;
    }
    // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace
    if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace) {
        // then check_designated_argument
        goto check_designated_argument$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState comma_else
comma_else$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::CommaElse;
    if (std::string_view(tokEnd, 2) == "=>"sv) {
        tokEnd += 2;
        // emitToken TokenKind::CommaElseExpr
        carriedEmitTokenKind = TokenKind::CommaElseExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState check_designated_argument
check_designated_argument$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
            // callArgument
            argumentPosition = addCallArgument(argumentPosition, Word());
            // emitToken TokenKind::CallArgument
            emitToken(TokenKind::CallArgument, tokBegin, 0, state);
            // -> expression
            goto expression$keyword_check;
        }
        // argumentName = this_identifier
        argumentName = this_identifier;
        // next maybe_designated_argument
        goto maybe_designated_argument$no_emit;
    }
    // callArgument
    argumentPosition = addCallArgument(argumentPosition, Word());
    // emitToken TokenKind::CallArgument
    emitToken(TokenKind::CallArgument, tokBegin, 0, state);
    // then expression
    goto expression$as_then;

    // LinearState maybe_designated_argument
maybe_designated_argument$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::MaybeDesignatedArgument;
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // callArgument argumentName
            argumentPosition = addCallArgument(argumentPosition, argumentName);
            // emitToken TokenKind::CallArgument, argumentName
            carriedEmitTokenKind = TokenKind::CallArgument;
            carriedEmitTokenData = argumentName.toUint();
            // next expression
            goto expression$with_emit;
        }
    }
    // callArgument
    argumentPosition = addCallArgument(argumentPosition, Word());
    // emitToken TokenKind::CallArgument
    emitToken(TokenKind::CallArgument, tokBegin, 0, state);
    // emitToken TokenKind::IdentifierExpr, argumentName
    emitToken(TokenKind::IdentifierExpr, tokBegin, argumentName.toUint(), state);
    // then after_expression
    goto after_expression$as_then;

    // LinearState first_argument_paren
first_argument_paren$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentParen;
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::Paren
    scopePosition = pushScope(scopePosition, ScopeKind::Paren);
    // then check_designated_argument
    goto check_designated_argument$as_then;

    // LinearState first_argument_square
first_argument_square$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentSquare;
    if (std::string_view(tokEnd, 1) == "]"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::Square
    scopePosition = pushScope(scopePosition, ScopeKind::Square);
    // then check_designated_argument
    goto check_designated_argument$as_then;

    // LinearState first_argument_brace
first_argument_brace$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentBrace;
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, state);
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::Brace
    scopePosition = pushScope(scopePosition, ScopeKind::Brace);
    // then check_designated_argument
    goto check_designated_argument$as_then;

    // LinearState access_punctuation
access_punctuation$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AccessPunctuation;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        // emitToken tokenKind, this_identifier
        carriedEmitTokenKind = tokenKind;
        carriedEmitTokenData = this_identifier.toUint();
        // next after_expression
        goto after_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState single_or_compound_statement
single_or_compound_statement$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
single_or_compound_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::SingleOrCompoundStatement;
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::CompoundStmt
        scopePosition = pushScope(scopePosition, ScopeKind::CompoundStmt);
        // pushScope ScopeKind::PlainStatement
        scopePosition = pushScope(scopePosition, ScopeKind::PlainStatement);
        // emitToken TokenKind::CompoundStmt
        carriedEmitTokenKind = TokenKind::CompoundStmt;
        carriedEmitTokenData = 0;
        // next statement
        goto statement$with_emit;
    }
    // then statement
    goto statement$as_then;

    // LinearState after_statement
after_statement$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterStatement;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED after_statement$keyword_check:
            if (this_identifier == words["else"]) {
                // popScope ScopeKind::IfBranch
                {
                    auto result = popScope(scopePosition, ScopeKind::IfBranch);
                    if (result == nullptr) {
                        errorToken = LexerToken::Else;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::ElseBranch
                scopePosition = pushScope(scopePosition, ScopeKind::ElseBranch);
                // next else_branch
                goto else_branch$no_emit;
            }
            // ifScope ScopeKind::FunctionBody
            if (scopePosition[0] == ScopeKind::FunctionBody) {
                // popScope ScopeKind::FunctionBody
                {
                    auto result = popScope(scopePosition, ScopeKind::FunctionBody);
                    if (result == nullptr) {
                        goto error$as_then;
                    }
                    scopePosition = result;
                }
                // then after_declaration
                // endDeclaration
                endDeclaration(state);
                // ifScope ScopeKind::Type
                if (scopePosition[0] == ScopeKind::Type) {
                    // then member_declaration
                    goto member_declaration$keyword_check;
                }
                // ifScope ScopeKind::Namespace
                if (scopePosition[0] == ScopeKind::Namespace) {
                    // then namespace_declaration
                    goto namespace_declaration$keyword_check;
                }
                // -> error
                goto error$keyword_check;
            }
            // ifScope ScopeKind::Type, ScopeKind::Namespace
            if (scopePosition[0] == ScopeKind::Type || scopePosition[0] == ScopeKind::Namespace) {
                // then after_declaration
                // endDeclaration
                endDeclaration(state);
                // ifScope ScopeKind::Type
                if (scopePosition[0] == ScopeKind::Type) {
                    // then member_declaration
                    goto member_declaration$keyword_check;
                }
                // ifScope ScopeKind::Namespace
                if (scopePosition[0] == ScopeKind::Namespace) {
                    // then namespace_declaration
                    goto namespace_declaration$keyword_check;
                }
                // -> error
                goto error$keyword_check;
            }
            // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement
            {
                auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement);
                if (result == nullptr) {
                    goto error$as_then;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::PlainStatement
            scopePosition = pushScope(scopePosition, ScopeKind::PlainStatement);
            // -> statement
            goto statement$keyword_check;
        }
        // ifScope ScopeKind::FunctionBody
        if (scopePosition[0] == ScopeKind::FunctionBody) {
            // popScope ScopeKind::FunctionBody
            {
                auto result = popScope(scopePosition, ScopeKind::FunctionBody);
                if (result == nullptr) {
                    goto error$as_then;
                }
                scopePosition = result;
            }
            // then after_declaration
            // endDeclaration
            endDeclaration(state);
            // ifScope ScopeKind::Type
            if (scopePosition[0] == ScopeKind::Type) {
                // then member_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // commitDeclaration DeclarationKind::Member, this_identifier
                this_declaration = commitDeclaration<DeclarationKind::Member>(this_identifier, tokBegin, declarationBegin, state);
                // emitToken TokenKind::MemberDecl, this_declaration
                carriedEmitTokenKind = TokenKind::MemberDecl;
                carriedEmitTokenData = this_declaration.toUint();
                // next after_variable_declaration_id
                goto after_variable_declaration_id$with_emit;
            }
            // ifScope ScopeKind::Namespace
            if (scopePosition[0] == ScopeKind::Namespace) {
                // then namespace_declaration
                // -> templated_declaration
                // -> no_declaration
                // -> error
                // error
                errorToken = LexerToken::Identifier;
                goto handle_parse_error;
            }
            // -> error
            // error
            errorToken = LexerToken::Identifier;
            goto handle_parse_error;
        }
        // ifScope ScopeKind::Type, ScopeKind::Namespace
        if (scopePosition[0] == ScopeKind::Type || scopePosition[0] == ScopeKind::Namespace) {
            // then after_declaration
            // endDeclaration
            endDeclaration(state);
            // ifScope ScopeKind::Type
            if (scopePosition[0] == ScopeKind::Type) {
                // then member_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // commitDeclaration DeclarationKind::Member, this_identifier
                this_declaration = commitDeclaration<DeclarationKind::Member>(this_identifier, tokBegin, declarationBegin, state);
                // emitToken TokenKind::MemberDecl, this_declaration
                carriedEmitTokenKind = TokenKind::MemberDecl;
                carriedEmitTokenData = this_declaration.toUint();
                // next after_variable_declaration_id
                goto after_variable_declaration_id$with_emit;
            }
            // ifScope ScopeKind::Namespace
            if (scopePosition[0] == ScopeKind::Namespace) {
                // then namespace_declaration
                // -> templated_declaration
                // -> no_declaration
                // -> error
                // error
                errorToken = LexerToken::Identifier;
                goto handle_parse_error;
            }
            // -> error
            // error
            errorToken = LexerToken::Identifier;
            goto handle_parse_error;
        }
        // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement
        {
            auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::PlainStatement
        scopePosition = pushScope(scopePosition, ScopeKind::PlainStatement);
        // -> statement
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::IdentifierExpr, this_identifier
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = this_identifier.toUint();
        // next after_expression
        goto after_expression$with_emit;
    }
    // ifScope ScopeKind::FunctionBody
    if (scopePosition[0] == ScopeKind::FunctionBody) {
        // popScope ScopeKind::FunctionBody
        {
            auto result = popScope(scopePosition, ScopeKind::FunctionBody);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // then after_declaration
        goto after_declaration$as_then;
    }
    // ifScope ScopeKind::Type, ScopeKind::Namespace
    if (scopePosition[0] == ScopeKind::Type || scopePosition[0] == ScopeKind::Namespace) {
        // then after_declaration
        goto after_declaration$as_then;
    }
    // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement
    {
        auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement);
        if (result == nullptr) {
            goto error$as_then;
        }
        scopePosition = result;
    }
    // pushScope ScopeKind::PlainStatement
    scopePosition = pushScope(scopePosition, ScopeKind::PlainStatement);
    // then statement
    goto statement$as_then;

    // SwitchState statement
statement$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
statement$no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::Statement;
statement$as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, state);
        goto statement$no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, state);
        goto statement$no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::ExclaimEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::LogicalNotExpr
        carriedEmitTokenKind = TokenKind::LogicalNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::PercentEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Percent;
        goto handle_parse_error;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                // error
                errorToken = LexerToken::AmpAmpEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::AmpAmp;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::AmpEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Amp;
        goto handle_parse_error;
    }
    case '(': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitCallToken TokenKind::ParenthesizedExpr
        argumentPosition = emitCallToken(argumentPosition, TokenKind::ParenthesizedExpr, tokBegin, state);
        // next first_argument_paren
        goto first_argument_paren$no_emit;
    }
    case ')': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::RightParen;
        goto handle_parse_error;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::StarEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::DereferenceExpr
        carriedEmitTokenKind = TokenKind::DereferenceExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // emitToken TokenKind::PreIncrementExpr
            carriedEmitTokenKind = TokenKind::PreIncrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::PlusEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::PlusExpr
        carriedEmitTokenKind = TokenKind::PlusExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ',': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Comma;
        goto handle_parse_error;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // emitToken TokenKind::PreDecrementExpr
            carriedEmitTokenKind = TokenKind::PreDecrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::MinusEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::MinusGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::NegateExpr
        carriedEmitTokenKind = TokenKind::NegateExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Point;
        goto handle_parse_error;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, state);
            goto statement$no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, state);
            goto statement$no_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::SlashEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Slash;
        goto handle_parse_error;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::ColonColon;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Colon;
        goto handle_parse_error;
    }
    case ';': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::SemiColon;
        goto handle_parse_error;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                // error
                errorToken = LexerToken::LessLessEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::LessLess;
            goto handle_parse_error;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                // error
                errorToken = LexerToken::LessEqualGreater;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::LessEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Less;
        goto handle_parse_error;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::EqualEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::EqualGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Equal;
        goto handle_parse_error;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::GreaterEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                // error
                errorToken = LexerToken::GreaterGreaterEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::GreaterGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Greater;
        goto handle_parse_error;
    }
    case '[': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::LeftSqure;
        goto handle_parse_error;
    }
    case ']': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::RightSqure;
        goto handle_parse_error;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::HatEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Hat;
        goto handle_parse_error;
    }
    case '{': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::LeftBrace;
        goto handle_parse_error;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::VertEqual;
            goto handle_parse_error;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                // error
                errorToken = LexerToken::VertVertEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            // error
            errorToken = LexerToken::VertVert;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        // error
        errorToken = LexerToken::Vert;
        goto handle_parse_error;
    }
    case '}': {
        tokEnd += 1;
        // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement
        {
            auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement);
            if (result == nullptr) {
                errorToken = LexerToken::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::CompoundStmt
        {
            auto result = popScope(scopePosition, ScopeKind::CompoundStmt);
            if (result == nullptr) {
                errorToken = LexerToken::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitToken TokenKind::EmptyNode
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_statement
        goto after_statement$with_emit;
    }
    case '~': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::BitwiseNotExpr
        carriedEmitTokenKind = TokenKind::BitwiseNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        do {
            tokEnd += 1;
        } while (tokEnd[0] >= '0' && tokEnd[0] <= '9');
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::LiteralExpr
        carriedEmitTokenKind = TokenKind::LiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::LiteralExpr
        carriedEmitTokenKind = TokenKind::LiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '#':
    case '$':
    case '_':
        goto statement$word_case;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
statement$word_case:
    {
        auto wordAndPos = readWord(tokEnd, state);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (this_identifier.keyword()) {
    LABEL_MAYBE_UNUSED statement$keyword_check:
        if (this_identifier == words["if"]) {
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // pushScope ScopeKind::IfExprOrStmt
            scopePosition = pushScope(scopePosition, ScopeKind::IfExprOrStmt);
            // next expression
            goto expression$no_emit;
        }
        if (this_identifier == words["let"]) {
            // next check_var_after_let
            goto check_var_after_let$no_emit;
        }
        if (this_identifier == words["var"]) {
            // tokenKind = TokenKind::VarStmt
            tokenKind = TokenKind::VarStmt;
            // next variable_declaration
            goto variable_declaration$no_emit;
        }
        if (this_identifier == words["return"]) {
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::ReturnStmt
            carriedEmitTokenKind = TokenKind::ReturnStmt;
            carriedEmitTokenData = 0;
            // next after_return
            goto after_return$with_emit;
        }
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        goto expression$keyword_check;
    }
    // pushScope ScopeKind::LeftExpr
    scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
    // -> expression
    // emitToken TokenKind::IdentifierExpr, this_identifier
    carriedEmitTokenKind = TokenKind::IdentifierExpr;
    carriedEmitTokenData = this_identifier.toUint();
    // next after_expression
    goto after_expression$with_emit;

    // LinearState after_return
after_return$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_return$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterReturn;
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // popScope ScopeKind::RightExpr
        {
            auto result = popScope(scopePosition, ScopeKind::RightExpr);
            if (result == nullptr) {
                errorToken = LexerToken::SemiColon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // updateKind TokenKind::EmptyReturnStmt
        state.parseOutput.tokens.back().setKind(TokenKind::EmptyReturnStmt);
        // next after_statement
        goto after_statement$no_emit;
    }
    // then expression
    goto expression$as_then;

    // LinearState else_branch
else_branch$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::ElseBranch;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // emitToken TokenKind::ElseStmt
            carriedEmitTokenKind = TokenKind::ElseStmt;
            carriedEmitTokenData = 0;
            // next single_or_compound_statement
            goto single_or_compound_statement$with_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState check_var_after_let
check_var_after_let$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::CheckVarAfterLet;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED check_var_after_let$keyword_check:
            if (this_identifier == words["var"]) {
                // tokenKind = TokenKind::VarStmt
                tokenKind = TokenKind::VarStmt;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            // tokenKind = TokenKind::LetStmt
            tokenKind = TokenKind::LetStmt;
            // -> variable_declaration
            // -> error
            goto error$keyword_check;
        }
        // tokenKind = TokenKind::LetStmt
        tokenKind = TokenKind::LetStmt;
        // -> variable_declaration
        // emitToken tokenKind, this_identifier
        carriedEmitTokenKind = tokenKind;
        carriedEmitTokenData = this_identifier.toUint();
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // tokenKind = TokenKind::LetStmt
    tokenKind = TokenKind::LetStmt;
    // then variable_declaration
    goto variable_declaration$as_then;

    // LinearState variable_declaration
variable_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::VariableDeclaration;
variable_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        // emitToken tokenKind, this_identifier
        carriedEmitTokenKind = tokenKind;
        carriedEmitTokenData = this_identifier.toUint();
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_variable_declaration_id
after_variable_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_variable_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterVariableDeclarationId;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // next expression
            goto expression$no_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AssignStmt
            carriedEmitTokenKind = TokenKind::AssignStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // emitToken TokenKind::AssignStmt
        emitToken(TokenKind::AssignStmt, tokBegin, 0, state);
        // emitToken TokenKind::ExpressionStmt
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_statement
        goto after_statement$with_emit;
    }
    if (std::string_view(tokEnd, 1) == ","sv) {
        tokEnd += 1;
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                errorToken = LexerToken::Comma;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::Parameter
        scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
        // emitToken TokenKind::AssignStmt
        emitToken(TokenKind::AssignStmt, tokBegin, 0, state);
        // emitToken TokenKind::ExpressionStmt
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next parameter
        goto parameter$with_emit;
    }
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                errorToken = LexerToken::RightParen;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitToken TokenKind::AssignStmt
        emitToken(TokenKind::AssignStmt, tokBegin, 0, state);
        // emitToken TokenKind::ExpressionStmt
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_parameters
        goto after_parameters$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_parameters
after_parameters$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_parameters$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterParameters;
    // emitToken TokenKind::EmptyNode
    emitToken(TokenKind::EmptyNode, tokBegin, 0, state);
    // ifScope ScopeKind::FunctionParameters
    if (scopePosition[0] == ScopeKind::FunctionParameters) {
        // popScope ScopeKind::FunctionParameters
        {
            auto result = popScope(scopePosition, ScopeKind::FunctionParameters);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // then after_function_parameters
        goto after_function_parameters$as_then;
    }
    // ifScope ScopeKind::TemplateParameters
    if (scopePosition[0] == ScopeKind::TemplateParameters) {
        // popScope ScopeKind::TemplateParameters
        {
            auto result = popScope(scopePosition, ScopeKind::TemplateParameters);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // then after_template_parameters
        goto after_template_parameters$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState first_parameter
first_parameter$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
first_parameter$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FirstParameter;
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // next after_parameters
        goto after_parameters$no_emit;
    }
    // pushScope ScopeKind::Parameter
    scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
    // then parameter
    goto parameter$as_then;

    // LinearState parameter
parameter$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
parameter$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::Parameter;
parameter$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED parameter$keyword_check:
            if (this_identifier == words["in"]) {
                // tokenKind = TokenKind::InParameter
                tokenKind = TokenKind::InParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (this_identifier == words["inout"]) {
                // tokenKind = TokenKind::InOutParameter
                tokenKind = TokenKind::InOutParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (this_identifier == words["out"]) {
                // tokenKind = TokenKind::OutParameter
                tokenKind = TokenKind::OutParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (this_identifier == words["let"]) {
                // tokenKind = TokenKind::LetParameter
                tokenKind = TokenKind::LetParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (this_identifier == words["var"]) {
                // tokenKind = TokenKind::VarParameter
                tokenKind = TokenKind::VarParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            // tokenKind = TokenKind::ImplicitKindParameter
            tokenKind = TokenKind::ImplicitKindParameter;
            // -> variable_declaration
            // -> error
            goto error$keyword_check;
        }
        // tokenKind = TokenKind::ImplicitKindParameter
        tokenKind = TokenKind::ImplicitKindParameter;
        // -> variable_declaration
        // emitToken tokenKind, this_identifier
        carriedEmitTokenKind = tokenKind;
        carriedEmitTokenData = this_identifier.toUint();
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // tokenKind = TokenKind::ImplicitKindParameter
    tokenKind = TokenKind::ImplicitKindParameter;
    // then variable_declaration
    goto variable_declaration$as_then;

    // LinearState no_declaration
no_declaration$as_then:
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Namespace, ScopeKind::Type
        {
            auto result = popScope(scopePosition, ScopeKind::Namespace, ScopeKind::Type);
            if (result == nullptr) {
                errorToken = LexerToken::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // next after_declaration
        goto after_declaration$no_emit;
    }
    if (tokEnd[0] == '\0') {
        emitToken(TokenKind::EOS, tokBegin, 0, state);
        emitWhitespace(WhitespaceKind::EOS, tokBegin, tokEnd, state);
        goto exit;
    }
    // then error
    goto error$as_then;

    // LinearState namespace_declaration
namespace_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::NamespaceDeclaration;
namespace_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED namespace_declaration$keyword_check:
            if (this_identifier == words["namespace"]) {
                // next namespace_declaration_id
                goto namespace_declaration_id$no_emit;
            }
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
        // -> templated_declaration
        // -> no_declaration
        // -> error
        // error
        errorToken = LexerToken::Identifier;
        goto handle_parse_error;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState namespace_declaration_id
namespace_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::NamespaceDeclarationId;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        // rememberDeclarationBegin
        declarationBegin = state.parseOutput.currentToken();
        // commitDeclaration DeclarationKind::Namespace, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Namespace>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::NamespaceDecl, this_declaration
        carriedEmitTokenKind = TokenKind::NamespaceDecl;
        carriedEmitTokenData = this_declaration.toUint();
        // next after_namespace_declaration_id
        goto after_namespace_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_namespace_declaration_id
after_namespace_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_namespace_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterNamespaceDeclarationId;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // next namespace_declaration_body
            goto namespace_declaration_body$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState namespace_declaration_body
namespace_declaration_body$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::NamespaceDeclarationBody;
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::Namespace
        scopePosition = pushScope(scopePosition, ScopeKind::Namespace);
        // next namespace_declaration
        goto namespace_declaration$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState templated_declaration
templated_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED templated_declaration$keyword_check:
            if (this_identifier == words["template"]) {
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next after_template
                goto after_template$no_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // tokenKind = TokenKind::StructTypeDecl
                tokenKind = TokenKind::StructTypeDecl;
                // next type_declaration_id
                goto type_declaration_id$no_emit;
            }
            if (this_identifier == words["object"]) {
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // tokenKind = TokenKind::ObjectTypeDecl
                tokenKind = TokenKind::ObjectTypeDecl;
                // next type_declaration_id
                goto type_declaration_id$no_emit;
            }
            if (this_identifier == words["static"]) {
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next after_static
                goto after_static$no_emit;
            }
            // -> no_declaration
            // -> error
            goto error$keyword_check;
        }
        // -> no_declaration
        // -> error
        // error
        errorToken = LexerToken::Identifier;
        goto handle_parse_error;
    }
    // then no_declaration
    goto no_declaration$as_then;

    // LinearState templated_declaration_with_attributes
templated_declaration_with_attributes$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
templated_declaration_with_attributes$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::TemplatedDeclarationWithAttributes;
templated_declaration_with_attributes$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED templated_declaration_with_attributes$keyword_check:
            if (this_identifier == words["template"]) {
                // next after_template
                goto after_template$no_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // tokenKind = TokenKind::StructTypeDecl
                tokenKind = TokenKind::StructTypeDecl;
                // next type_declaration_id
                goto type_declaration_id$no_emit;
            }
            if (this_identifier == words["object"]) {
                // tokenKind = TokenKind::ObjectTypeDecl
                tokenKind = TokenKind::ObjectTypeDecl;
                // next type_declaration_id
                goto type_declaration_id$no_emit;
            }
            if (this_identifier == words["static"]) {
                // next after_static
                goto after_static$no_emit;
            }
            // -> error
            goto error$keyword_check;
        }
        // -> error
        // error
        errorToken = LexerToken::Identifier;
        goto handle_parse_error;
    }
    // then error
    goto error$as_then;

    // LinearState after_template
after_template$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterTemplate;
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // pushScope ScopeKind::TemplateParameters
        scopePosition = pushScope(scopePosition, ScopeKind::TemplateParameters);
        // emitToken TokenKind::TemplateAttribute
        carriedEmitTokenKind = TokenKind::TemplateAttribute;
        carriedEmitTokenData = 0;
        // next first_parameter
        goto first_parameter$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_template_parameters
after_template_parameters$as_then:
    // then templated_declaration_with_attributes
    goto templated_declaration_with_attributes$as_then;

    // LinearState function_declaration_id
function_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FunctionDeclarationId;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        // commitDeclaration DeclarationKind::Function, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Function>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::FunctionDecl, this_declaration
        carriedEmitTokenKind = TokenKind::FunctionDecl;
        carriedEmitTokenData = this_declaration.toUint();
        // next after_function_declaration_id
        goto after_function_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_function_declaration_id
after_function_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_function_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterFunctionDeclarationId;
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // pushScope ScopeKind::FunctionParameters
        scopePosition = pushScope(scopePosition, ScopeKind::FunctionParameters);
        // next first_parameter
        goto first_parameter$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_function_parameters
after_function_parameters$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // pushScope ScopeKind::FunctionBody
            scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
            // emitToken TokenKind::FunctionBody
            carriedEmitTokenKind = TokenKind::FunctionBody;
            carriedEmitTokenData = 0;
            // next single_or_compound_statement
            goto single_or_compound_statement$with_emit;
        }
    }
    if (std::string_view(tokEnd, 2) == "->"sv) {
        tokEnd += 2;
        // pushScope ScopeKind::ReturnType
        scopePosition = pushScope(scopePosition, ScopeKind::ReturnType);
        // emitToken TokenKind::ReturnType
        carriedEmitTokenKind = TokenKind::ReturnType;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    if (std::string_view(tokEnd, 2) == "=>"sv) {
        tokEnd += 2;
        // pushScope ScopeKind::FunctionBody
        scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // emitToken TokenKind::BodyExpr
        carriedEmitTokenKind = TokenKind::BodyExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    if (std::string_view(tokEnd, 3) == "<=>"sv) {
        tokEnd += 3;
        // pushScope ScopeKind::FunctionBody
        scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // emitToken TokenKind::BodyExpr
        carriedEmitTokenKind = TokenKind::BodyExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState type_declaration_id
type_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::TypeDeclarationId;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        // commitDeclaration DeclarationKind::Type, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Type>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken tokenKind, this_declaration
        carriedEmitTokenKind = tokenKind;
        carriedEmitTokenData = this_declaration.toUint();
        // next after_type_declaration_id
        goto after_type_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_type_declaration_id
after_type_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_type_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterTypeDeclarationId;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // next type_declaration_body
            goto type_declaration_body$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState type_declaration_body
type_declaration_body$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::TypeDeclarationBody;
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::Type
        scopePosition = pushScope(scopePosition, ScopeKind::Type);
        // next member_declaration
        goto member_declaration$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState member_declaration
member_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::MemberDeclaration;
member_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED member_declaration$keyword_check:
            if (this_identifier == words["has"]) {
                // pushScope ScopeKind::HasTypeExpr
                scopePosition = pushScope(scopePosition, ScopeKind::HasTypeExpr);
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // commitDeclaration DeclarationKind::HasMember
                this_declaration = commitDeclaration<DeclarationKind::HasMember>(Word(), tokBegin, declarationBegin, state);
                // emitToken TokenKind::HasMemberDecl, this_declaration
                carriedEmitTokenKind = TokenKind::HasMemberDecl;
                carriedEmitTokenData = this_declaration.toUint();
                // next expression
                goto expression$with_emit;
            }
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
        // rememberDeclarationBegin
        declarationBegin = state.parseOutput.currentToken();
        // commitDeclaration DeclarationKind::Member, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Member>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::MemberDecl, this_declaration
        carriedEmitTokenKind = TokenKind::MemberDecl;
        carriedEmitTokenData = this_declaration.toUint();
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState after_static
after_static$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterStatic;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
        LABEL_MAYBE_UNUSED after_static$keyword_check:
            if (this_identifier == words["let"]) {
                // next static_let_variable_declaration
                goto static_let_variable_declaration$no_emit;
            }
            if (this_identifier == words["var"]) {
                // next static_var_variable_declaration
                goto static_var_variable_declaration$no_emit;
            }
            // -> error
            goto error$keyword_check;
        }
        // commitDeclaration DeclarationKind::StaticValue, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticValue>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::StaticLetDecl, this_declaration
        carriedEmitTokenKind = TokenKind::StaticLetDecl;
        carriedEmitTokenData = this_declaration.toUint();
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_let_variable_declaration
static_let_variable_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::StaticLetVariableDeclaration;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        // commitDeclaration DeclarationKind::StaticValue, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticValue>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::StaticLetDecl, this_declaration
        carriedEmitTokenKind = TokenKind::StaticLetDecl;
        carriedEmitTokenData = this_declaration.toUint();
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_var_variable_declaration
static_var_variable_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::StaticVarVariableDeclaration;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (this_identifier.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        // commitDeclaration DeclarationKind::StaticObject, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticObject>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::StaticVarDecl, this_declaration
        carriedEmitTokenKind = TokenKind::StaticVarDecl;
        carriedEmitTokenData = this_declaration.toUint();
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_declaration
after_declaration$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterDeclaration;
after_declaration$as_then:
    // endDeclaration
    endDeclaration(state);
    // ifScope ScopeKind::Type
    if (scopePosition[0] == ScopeKind::Type) {
        // then member_declaration
        goto member_declaration$as_then;
    }
    // ifScope ScopeKind::Namespace
    if (scopePosition[0] == ScopeKind::Namespace) {
        // then namespace_declaration
        goto namespace_declaration$as_then;
    }
    // then error
    goto error$as_then;

    // SwitchState error
error$as_then:
    switch (tokEnd[0]) {
    case '\n':
    case '\r':
        VERIFY_NOT_REACHED();
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::ExclaimEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Exclaim;
        goto handle_parse_error;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::PercentEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Percent;
        goto handle_parse_error;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                errorToken = LexerToken::AmpAmpEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = LexerToken::AmpAmp;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::AmpEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Amp;
        goto handle_parse_error;
    }
    case '(': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::LeftParen;
        goto handle_parse_error;
    }
    case ')': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::RightParen;
        goto handle_parse_error;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::StarEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Star;
        goto handle_parse_error;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::PlusPlus;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::PlusEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Plus;
        goto handle_parse_error;
    }
    case ',': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::Comma;
        goto handle_parse_error;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::MinusMinus;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::MinusEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::MinusGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Minus;
        goto handle_parse_error;
    }
    case '.': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::Point;
        goto handle_parse_error;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            VERIFY_NOT_REACHED();
        }
        if (next == '/') {
            tokEnd += 2;
            VERIFY_NOT_REACHED();
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::SlashEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Slash;
        goto handle_parse_error;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::ColonColon;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Colon;
        goto handle_parse_error;
    }
    case ';': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::SemiColon;
        goto handle_parse_error;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                errorToken = LexerToken::LessLessEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = LexerToken::LessLess;
            goto handle_parse_error;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // error
                errorToken = LexerToken::LessEqualGreater;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = LexerToken::LessEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Less;
        goto handle_parse_error;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::EqualEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::EqualGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Equal;
        goto handle_parse_error;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::GreaterEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                errorToken = LexerToken::GreaterGreaterEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = LexerToken::GreaterGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Greater;
        goto handle_parse_error;
    }
    case '[': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::LeftSqure;
        goto handle_parse_error;
    }
    case ']': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::RightSqure;
        goto handle_parse_error;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::HatEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Hat;
        goto handle_parse_error;
    }
    case '{': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::LeftBrace;
        goto handle_parse_error;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = LexerToken::VertEqual;
            goto handle_parse_error;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                errorToken = LexerToken::VertVertEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = LexerToken::VertVert;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = LexerToken::Vert;
        goto handle_parse_error;
    }
    case '}': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::RightBrace;
        goto handle_parse_error;
    }
    case '~': {
        tokEnd += 1;
        // error
        errorToken = LexerToken::Tilde;
        goto handle_parse_error;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        do {
            tokEnd += 1;
        } while (tokEnd[0] >= '0' && tokEnd[0] <= '9');
        // error
        errorToken = LexerToken::Literal;
        goto handle_parse_error;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // error
        errorToken = LexerToken::Literal;
        goto handle_parse_error;
    }
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '#':
    case '$':
    case '_':
        goto error$word_case;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
error$word_case:
    {
        auto wordAndPos = readWord(tokEnd, state);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (this_identifier.keyword()) {
    LABEL_MAYBE_UNUSED error$keyword_check:
        if (this_identifier == words["analysis"]) {
            // error
            errorToken = LexerToken::Analysis;
            goto handle_parse_error;
        }
        if (this_identifier == words["assert"]) {
            // error
            errorToken = LexerToken::Assert;
            goto handle_parse_error;
        }
        if (this_identifier == words["assign"]) {
            // error
            errorToken = LexerToken::Assign;
            goto handle_parse_error;
        }
        if (this_identifier == words["break"]) {
            // error
            errorToken = LexerToken::Break;
            goto handle_parse_error;
        }
        if (this_identifier == words["catch"]) {
            // error
            errorToken = LexerToken::Catch;
            goto handle_parse_error;
        }
        if (this_identifier == words["continue"]) {
            // error
            errorToken = LexerToken::Continue;
            goto handle_parse_error;
        }
        if (this_identifier == words["do"]) {
            // error
            errorToken = LexerToken::Do;
            goto handle_parse_error;
        }
        if (this_identifier == words["elif"]) {
            // error
            errorToken = LexerToken::Elif;
            goto handle_parse_error;
        }
        if (this_identifier == words["else"]) {
            // error
            errorToken = LexerToken::Else;
            goto handle_parse_error;
        }
        if (this_identifier == words["fn"]) {
            // error
            errorToken = LexerToken::Fn;
            goto handle_parse_error;
        }
        if (this_identifier == words["for"]) {
            // error
            errorToken = LexerToken::For;
            goto handle_parse_error;
        }
        if (this_identifier == words["forward"]) {
            // error
            errorToken = LexerToken::Forward;
            goto handle_parse_error;
        }
        if (this_identifier == words["guard"]) {
            // error
            errorToken = LexerToken::Guard;
            goto handle_parse_error;
        }
        if (this_identifier == words["has"]) {
            // error
            errorToken = LexerToken::Has;
            goto handle_parse_error;
        }
        if (this_identifier == words["if"]) {
            // error
            errorToken = LexerToken::If;
            goto handle_parse_error;
        }
        if (this_identifier == words["in"]) {
            // error
            errorToken = LexerToken::In;
            goto handle_parse_error;
        }
        if (this_identifier == words["incomplete"]) {
            // error
            errorToken = LexerToken::Incomplete;
            goto handle_parse_error;
        }
        if (this_identifier == words["inout"]) {
            // error
            errorToken = LexerToken::Inout;
            goto handle_parse_error;
        }
        if (this_identifier == words["let"]) {
            // error
            errorToken = LexerToken::Let;
            goto handle_parse_error;
        }
        if (this_identifier == words["loop"]) {
            // error
            errorToken = LexerToken::Loop;
            goto handle_parse_error;
        }
        if (this_identifier == words["match"]) {
            // error
            errorToken = LexerToken::Match;
            goto handle_parse_error;
        }
        if (this_identifier == words["namespace"]) {
            // error
            errorToken = LexerToken::Namespace;
            goto handle_parse_error;
        }
        if (this_identifier == words["object"]) {
            // error
            errorToken = LexerToken::Object;
            goto handle_parse_error;
        }
        if (this_identifier == words["out"]) {
            // error
            errorToken = LexerToken::Out;
            goto handle_parse_error;
        }
        if (this_identifier == words["property"]) {
            // error
            errorToken = LexerToken::Property;
            goto handle_parse_error;
        }
        if (this_identifier == words["return"]) {
            // error
            errorToken = LexerToken::Return;
            goto handle_parse_error;
        }
        if (this_identifier == words["static"]) {
            // error
            errorToken = LexerToken::Static;
            goto handle_parse_error;
        }
        if (this_identifier == words["struct"]) {
            // error
            errorToken = LexerToken::Struct;
            goto handle_parse_error;
        }
        if (this_identifier == words["template"]) {
            // error
            errorToken = LexerToken::Template;
            goto handle_parse_error;
        }
        if (this_identifier == words["trait"]) {
            // error
            errorToken = LexerToken::Trait;
            goto handle_parse_error;
        }
        if (this_identifier == words["try"]) {
            // error
            errorToken = LexerToken::Try;
            goto handle_parse_error;
        }
        if (this_identifier == words["var"]) {
            // error
            errorToken = LexerToken::Var;
            goto handle_parse_error;
        }
        if (this_identifier == words["virtual"]) {
            // error
            errorToken = LexerToken::Virtual;
            goto handle_parse_error;
        }
        if (this_identifier == words["while"]) {
            // error
            errorToken = LexerToken::While;
            goto handle_parse_error;
        }
        if (this_identifier == words["with"]) {
            // error
            errorToken = LexerToken::With;
            goto handle_parse_error;
        }
        VERIFY_NOT_REACHED();
    }
    // error
    errorToken = LexerToken::Identifier;
    goto handle_parse_error;


exit:
    VERIFY(scopePosition == scopeBuffer.buffer + 1);
    VERIFY(scopeBuffer.buffer[0] == ScopeKind::Invalid);
    VERIFY(scopeBuffer.buffer[1] == ScopeKind::Namespace);
    VERIFY(argumentPosition == argumentBuffer.buffer);
    return;

handle_parse_error:
    errorHandler->invalidToken(errorToken, parseState, scopePosition, state);
    return;
}

std::string_view nameString(TokenKind kind) {
    switch (kind) {
#define TOKEN(kind, type, prec) \
    case TokenKind::kind:       \
        return #kind;

#include <parse/tokens.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

std::string_view nameString(ScopeKind kind) {
    switch (kind) {
#define SCOPE(kind)       \
    case ScopeKind::kind: \
        return #kind;
        ENUMERATE_SCOPE_KINDS
#undef SCOPE
    default:
        VERIFY_NOT_REACHED();
    }
}

}