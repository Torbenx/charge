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
    Struct,
    StaticVariable,
    Function,
    Member,
    HasMember,
    Enum,
    EnumValue,
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

static void updateCallArgument(Word* position, Word name) {
    uint32_t count = position[0].toUint();
    VERIFY(count != 0);
    position[-1] = name;
}

NO_INLINE static Word* endCall(Word* position, ParseState& state) {
    uint32_t count = position[0].toUint();
    auto& outputArgs = state.parseOutput.callArguments;
    CallArgumentsHandle handle { (uint32_t)outputArgs.size() };
    outputArgs.push_back(position[0]);
    outputArgs.insert(outputArgs.end(), position - count, position);
    position -= count + 2;
    state.parseOutput.tokens[position[1].toUint()].setData1(handle);
    return position;
}

static SourceLocation locationInCurrentLine(const char* position, ParseState& state) {
    return {
        0u,
        (uint32_t)state.parseOutput.lines.size() - 1,
        (uint32_t)(position - state.parseOutput.lines.back().begin)
    };
}

NO_INLINE static void emitToken(TokenKind kind, const char* begin, uint32_t data, ParseState& state) {
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

static sema::ProgramKind programKindForDeclaration(DeclarationKind kind) {
    switch (kind) {
    case DeclarationKind::Struct:
        return sema::ProgramKind::Struct;
    case DeclarationKind::Function:
        return sema::ProgramKind::Function;
    case DeclarationKind::StaticVariable:
        return sema::ProgramKind::Global;
    case DeclarationKind::Enum:
        return sema::ProgramKind::Enum;
    default:
        VERIFY_NOT_REACHED();
    }
}

template<DeclarationKind kind>
static sema::DeclarationValue commitDeclaration(Word name, const char* currentPosition, TokenHandle declarationBegin, ParseState& state) {
    // fmt::println("commitDeclaration {}", state.wordTable.view(name));
    if constexpr (kind == DeclarationKind::Member || kind == DeclarationKind::HasMember) {
        return state.pushMemberScope(kind == DeclarationKind::HasMember, name, declarationBegin, locationInCurrentLine(currentPosition, state));
    } else if constexpr (kind == DeclarationKind::EnumValue) {
        return state.pushEnumValueScope(name, declarationBegin, locationInCurrentLine(currentPosition, state));
    } else if constexpr (kind == DeclarationKind::Namespace) {
        return state.pushNamespaceScope(name);
    } else {
        return state.pushStaticScope(programKindForDeclaration(kind), name, declarationBegin, locationInCurrentLine(currentPosition, state));
    }
}

template<DeclarationKind kind>
static sema::DeclarationValue commitImplDeclaration(const char* currentPosition, TokenHandle declarationBegin, ParseState& state) {
    static_assert(kind == DeclarationKind::Struct || kind == DeclarationKind::Function || kind == DeclarationKind::Enum);
    return state.pushStaticImplScope(programKindForDeclaration(kind), declarationBegin, locationInCurrentLine(currentPosition, state));
}

static void endDeclaration(ParseState& state) {
    // fmt::println("endDeclaration {}", state.wordTable.view(state.currentScope()->name()));
    state.popScope();
}

using GlobalKind = sema::GlobalKind;
static void setGlobalKind(ParseState& state, GlobalKind kind) {
    sema::cast<sema::GlobalProgram>(state.currentProgram())->m_globalKind = kind;
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
    sema::DeclarationValue this_declaration = sema::INVALID_DECLARATION_VALUE;
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
    case State::Argument:
        goto argument$no_emit;
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
    case State::LetStatement:
        goto let_statement$no_emit;
    case State::VarStatement:
        goto var_statement$no_emit;
    case State::AfterReturn:
        goto after_return$no_emit;
    case State::ElseBranch:
        goto else_branch$no_emit;
    case State::AfterSimpleVariableDeclarationId:
        goto after_simple_variable_declaration_id$no_emit;
    case State::AfterVariableDeclarationId:
        goto after_variable_declaration_id$no_emit;
    case State::VariableType:
        goto variable_type$no_emit;
    case State::AfterVariableModifier:
        goto after_variable_modifier$no_emit;
    case State::AfterVariableUniqueModifier:
        goto after_variable_unique_modifier$no_emit;
    case State::AfterVariableSharedModifier:
        goto after_variable_shared_modifier$no_emit;
    case State::AfterVariableConstModifier:
        goto after_variable_const_modifier$no_emit;
    case State::AfterParameters:
        goto after_parameters$no_emit;
    case State::FirstParameter:
        goto first_parameter$no_emit;
    case State::Parameter:
        goto parameter$no_emit;
    case State::VarParameter:
        goto var_parameter$no_emit;
    case State::ImplExpression:
        goto impl_expression$no_emit;
    case State::AfterImplExpression:
        goto after_impl_expression$no_emit;
    case State::ImplAccessExpression:
        goto impl_access_expression$no_emit;
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
    case State::StructDeclarationId:
        goto struct_declaration_id$no_emit;
    case State::AfterStructDeclarationId:
        goto after_struct_declaration_id$no_emit;
    case State::StructDeclarationBody:
        goto struct_declaration_body$no_emit;
    case State::MemberDeclaration:
        goto member_declaration$no_emit;
    case State::EnumDeclarationId:
        goto enum_declaration_id$no_emit;
    case State::AfterEnumDeclarationId:
        goto after_enum_declaration_id$no_emit;
    case State::EnumDeclarationBody:
        goto enum_declaration_body$no_emit;
    case State::EnumValueDeclaration:
        goto enum_value_declaration$no_emit;
    case State::AfterEnumValueDeclarationId:
        goto after_enum_value_declaration_id$no_emit;
    case State::AfterStatic:
        goto after_static$no_emit;
    case State::StaticVarVariableDeclaration:
        goto static_var_variable_declaration$no_emit;
    case State::StaticOpenVariableDeclaration:
        goto static_open_variable_declaration$no_emit;
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
        goto expression$word_case_entry;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
expression$word_case_entry:
    {
        auto wordAndPos = readWord(tokEnd, state);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (sema::isKeyword(this_identifier)) {
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
LABEL_MAYBE_UNUSED expression$identifier_case:
    if (sema::isSpecialIdentifier(this_identifier)) {
    }
    // emitToken TokenKind::IdentifierExpr, this_identifier
    carriedEmitTokenKind = TokenKind::IdentifierExpr;
    carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
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
        // ifScope ScopeKind::ParenInImplExpr
        if (scopePosition[0] == ScopeKind::ParenInImplExpr) {
            // popScope ScopeKind::ParenInImplExpr
            {
                auto result = popScope(scopePosition, ScopeKind::ParenInImplExpr);
                if (result == nullptr) {
                    errorToken = LexerToken::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::EmptyNode
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next after_impl_expression
            goto after_impl_expression$with_emit;
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
            // next struct_declaration_body
            goto struct_declaration_body$no_emit;
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
        // ifScope ScopeKind::BraceInImplExpr
        if (scopePosition[0] == ScopeKind::BraceInImplExpr) {
            // popScope ScopeKind::BraceInImplExpr
            {
                auto result = popScope(scopePosition, ScopeKind::BraceInImplExpr);
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
            // next after_impl_expression
            goto after_impl_expression$with_emit;
        }
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
        goto after_expression$word_case_entry;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
after_expression$word_case_entry:
    {
        auto wordAndPos = readWord(tokEnd, state);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (sema::isKeyword(this_identifier)) {
        // -> error
        goto error$keyword_check;
    }
LABEL_MAYBE_UNUSED after_expression$identifier_case:
    if (sema::isSpecialIdentifier(this_identifier)) {
    }
    // -> error
    goto error$identifier_case;

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
        // ifScope ScopeKind::BraceInImplExpr
        if (scopePosition[0] == ScopeKind::BraceInImplExpr) {
            // popScope ScopeKind::BraceInImplExpr
            {
                auto result = popScope(scopePosition, ScopeKind::BraceInImplExpr);
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
            // next after_impl_expression
            goto after_impl_expression$with_emit;
        }
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
        if (sema::isKeyword(this_identifier)) {
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
            // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace, ScopeKind::BraceInImplExpr
            if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace || scopePosition[0] == ScopeKind::BraceInImplExpr) {
                // then argument
                // callArgument
                argumentPosition = addCallArgument(argumentPosition, Word());
                // emitToken TokenKind::CallArgument
                emitToken(TokenKind::CallArgument, tokBegin, 0, state);
                // -> check_designated_argument
                // -> expression
                goto expression$keyword_check;
            }
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED comma_after_expression$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // ifScope ScopeKind::Parameter
        if (scopePosition[0] == ScopeKind::Parameter) {
            // then parameter
            goto parameter$identifier_case;
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
            goto parameter$identifier_case;
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
            goto parameter$identifier_case;
        }
        // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace, ScopeKind::BraceInImplExpr
        if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace || scopePosition[0] == ScopeKind::BraceInImplExpr) {
            // then argument
            // callArgument
            argumentPosition = addCallArgument(argumentPosition, Word());
            // emitToken TokenKind::CallArgument
            emitToken(TokenKind::CallArgument, tokBegin, 0, state);
            // -> check_designated_argument
            goto check_designated_argument$identifier_case;
        }
        // -> error
        goto error$identifier_case;
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
    // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace, ScopeKind::BraceInImplExpr
    if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace || scopePosition[0] == ScopeKind::BraceInImplExpr) {
        // then argument
        goto argument$as_then;
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

    // LinearState argument
argument$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::Argument;
argument$as_then:
    // callArgument
    argumentPosition = addCallArgument(argumentPosition, Word());
    // emitToken TokenKind::CallArgument
    emitToken(TokenKind::CallArgument, tokBegin, 0, state);
    // then check_designated_argument
    goto check_designated_argument$as_then;

    // LinearState check_designated_argument
check_designated_argument$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED check_designated_argument$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // argumentName = this_identifier
        argumentName = this_identifier;
        // next maybe_designated_argument
        goto maybe_designated_argument$no_emit;
    }
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
            updateCallArgument(argumentPosition, argumentName);
            // next expression
            goto expression$no_emit;
        }
    }
    // emitToken TokenKind::IdentifierExpr, argumentName
    emitToken(TokenKind::IdentifierExpr, tokBegin, packData1(TokenKind::IdentifierExpr, argumentName), state);
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
    // then argument
    goto argument$as_then;

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
    // then argument
    goto argument$as_then;

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
    // then argument
    goto argument$as_then;

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
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED access_punctuation$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // emitToken tokenKind, this_identifier
        carriedEmitTokenKind = tokenKind;
        carriedEmitTokenData = packData1(tokenKind, this_identifier);
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
        if (sema::isKeyword(this_identifier)) {
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
                // ifScope ScopeKind::Struct
                if (scopePosition[0] == ScopeKind::Struct) {
                    // then member_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // ifScope ScopeKind::Namespace
                if (scopePosition[0] == ScopeKind::Namespace) {
                    // then namespace_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // ifScope ScopeKind::Enum
                if (scopePosition[0] == ScopeKind::Enum) {
                    // then enum_value_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // -> error
                goto error$keyword_check;
            }
            // ifScope ScopeKind::Struct, ScopeKind::Namespace, ScopeKind::Enum
            if (scopePosition[0] == ScopeKind::Struct || scopePosition[0] == ScopeKind::Namespace || scopePosition[0] == ScopeKind::Enum) {
                // then after_declaration
                // endDeclaration
                endDeclaration(state);
                // ifScope ScopeKind::Struct
                if (scopePosition[0] == ScopeKind::Struct) {
                    // then member_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // ifScope ScopeKind::Namespace
                if (scopePosition[0] == ScopeKind::Namespace) {
                    // then namespace_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // ifScope ScopeKind::Enum
                if (scopePosition[0] == ScopeKind::Enum) {
                    // then enum_value_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
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
    LABEL_MAYBE_UNUSED after_statement$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
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
            // ifScope ScopeKind::Struct
            if (scopePosition[0] == ScopeKind::Struct) {
                // then member_declaration
                goto member_declaration$identifier_case;
            }
            // ifScope ScopeKind::Namespace
            if (scopePosition[0] == ScopeKind::Namespace) {
                // then namespace_declaration
                goto namespace_declaration$identifier_case;
            }
            // ifScope ScopeKind::Enum
            if (scopePosition[0] == ScopeKind::Enum) {
                // then enum_value_declaration
                goto enum_value_declaration$identifier_case;
            }
            // -> error
            goto error$identifier_case;
        }
        // ifScope ScopeKind::Struct, ScopeKind::Namespace, ScopeKind::Enum
        if (scopePosition[0] == ScopeKind::Struct || scopePosition[0] == ScopeKind::Namespace || scopePosition[0] == ScopeKind::Enum) {
            // then after_declaration
            // endDeclaration
            endDeclaration(state);
            // ifScope ScopeKind::Struct
            if (scopePosition[0] == ScopeKind::Struct) {
                // then member_declaration
                goto member_declaration$identifier_case;
            }
            // ifScope ScopeKind::Namespace
            if (scopePosition[0] == ScopeKind::Namespace) {
                // then namespace_declaration
                goto namespace_declaration$identifier_case;
            }
            // ifScope ScopeKind::Enum
            if (scopePosition[0] == ScopeKind::Enum) {
                // then enum_value_declaration
                goto enum_value_declaration$identifier_case;
            }
            // -> error
            goto error$identifier_case;
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
        goto statement$identifier_case;
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
    // ifScope ScopeKind::Struct, ScopeKind::Namespace, ScopeKind::Enum
    if (scopePosition[0] == ScopeKind::Struct || scopePosition[0] == ScopeKind::Namespace || scopePosition[0] == ScopeKind::Enum) {
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
        goto statement$word_case_entry;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
statement$word_case_entry:
    {
        auto wordAndPos = readWord(tokEnd, state);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (sema::isKeyword(this_identifier)) {
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
            // next let_statement
            goto let_statement$no_emit;
        }
        if (this_identifier == words["var"]) {
            // next var_statement
            goto var_statement$no_emit;
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
        if (this_identifier == words["destroy"]) {
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::DestroyStmt
            carriedEmitTokenKind = TokenKind::DestroyStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (this_identifier == words["discard"]) {
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::DiscardStmt
            carriedEmitTokenKind = TokenKind::DiscardStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        goto expression$keyword_check;
    }
LABEL_MAYBE_UNUSED statement$identifier_case:
    if (sema::isSpecialIdentifier(this_identifier)) {
    }
    // pushScope ScopeKind::LeftExpr
    scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
    // -> expression
    goto expression$identifier_case;

    // LinearState let_statement
let_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::LetStatement;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED let_statement$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::LetValueDecl, this_identifier
        carriedEmitTokenKind = TokenKind::LetValueDecl;
        carriedEmitTokenData = packData1(TokenKind::LetValueDecl, this_identifier);
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState var_statement
var_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::VarStatement;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED var_statement$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::VarValueDecl, this_identifier
        carriedEmitTokenKind = TokenKind::VarValueDecl;
        carriedEmitTokenData = packData1(TokenKind::VarValueDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

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

    // LinearState after_simple_variable_declaration_id
after_simple_variable_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_simple_variable_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterSimpleVariableDeclarationId;
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
    // then after_variable_declaration_id
    goto after_variable_declaration_id$as_then;

    // LinearState after_variable_declaration_id
after_variable_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_variable_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterVariableDeclarationId;
after_variable_declaration_id$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // next variable_type
            goto variable_type$no_emit;
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

    // LinearState variable_type
variable_type$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::VariableType;
    if (std::string_view(tokEnd, 1) == "<"sv) {
        char next = tokEnd[1];
        if (next != '<' && next != '=') {
            tokEnd += 1;
            // pushScope ScopeKind::GenericCategoryExpression
            scopePosition = pushScope(scopePosition, ScopeKind::GenericCategoryExpression);
            // updateKind TokenKind::GenericCategoryVariableDecl
            state.parseOutput.tokens.back().setKind(TokenKind::GenericCategoryVariableDecl);
            // next impl_expression
            goto impl_expression$no_emit;
        }
    }
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED variable_type$keyword_check:
            if (this_identifier == words["unique"]) {
                // updateKind TokenKind::UniqueReferenceDecl
                state.parseOutput.tokens.back().setKind(TokenKind::UniqueReferenceDecl);
                // next after_variable_unique_modifier
                goto after_variable_unique_modifier$no_emit;
            }
            if (this_identifier == words["shared"]) {
                // updateKind TokenKind::SharedReferenceDecl
                state.parseOutput.tokens.back().setKind(TokenKind::SharedReferenceDecl);
                // next after_variable_shared_modifier
                goto after_variable_shared_modifier$no_emit;
            }
            if (this_identifier == words["const"]) {
                // updateKind TokenKind::ConstSharedReferenceDecl
                state.parseOutput.tokens.back().setKind(TokenKind::ConstSharedReferenceDecl);
                // next after_variable_const_modifier
                goto after_variable_const_modifier$no_emit;
            }
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED variable_type$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        goto expression$identifier_case;
    }
    // pushScope ScopeKind::VariableType
    scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
    // then expression
    goto expression$as_then;

    // LinearState after_variable_modifier
after_variable_modifier$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterVariableModifier;
after_variable_modifier$as_then:
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
    // pushScope ScopeKind::VariableType
    scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
    // then expression
    goto expression$as_then;

    // LinearState after_variable_unique_modifier
after_variable_unique_modifier$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterVariableUniqueModifier;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_variable_unique_modifier$keyword_check:
            if (this_identifier == words["const"]) {
                // updateKind TokenKind::ConstUniqueReferenceDecl
                state.parseOutput.tokens.back().setKind(TokenKind::ConstUniqueReferenceDecl);
                // next after_variable_modifier
                goto after_variable_modifier$no_emit;
            }
            // -> after_variable_modifier
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED after_variable_unique_modifier$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        goto expression$identifier_case;
    }
    // then after_variable_modifier
    goto after_variable_modifier$as_then;

    // LinearState after_variable_shared_modifier
after_variable_shared_modifier$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterVariableSharedModifier;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_variable_shared_modifier$keyword_check:
            if (this_identifier == words["const"]) {
                // updateKind TokenKind::ConstSharedReferenceDecl
                state.parseOutput.tokens.back().setKind(TokenKind::ConstSharedReferenceDecl);
                // next after_variable_modifier
                goto after_variable_modifier$no_emit;
            }
            // -> after_variable_modifier
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED after_variable_shared_modifier$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        goto expression$identifier_case;
    }
    // then after_variable_modifier
    goto after_variable_modifier$as_then;

    // LinearState after_variable_const_modifier
after_variable_const_modifier$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterVariableConstModifier;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_variable_const_modifier$keyword_check:
            if (this_identifier == words["shared"]) {
                // next after_variable_modifier
                goto after_variable_modifier$no_emit;
            }
            if (this_identifier == words["unique"]) {
                // updateKind TokenKind::ConstUniqueReferenceDecl
                state.parseOutput.tokens.back().setKind(TokenKind::ConstUniqueReferenceDecl);
                // next after_variable_modifier
                goto after_variable_modifier$no_emit;
            }
            // -> after_variable_modifier
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED after_variable_const_modifier$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        goto expression$identifier_case;
    }
    // then after_variable_modifier
    goto after_variable_modifier$as_then;

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
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED parameter$keyword_check:
            if (this_identifier == words["var"]) {
                // next var_parameter
                goto var_parameter$no_emit;
            }
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED parameter$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::LetValueDecl, this_identifier
        carriedEmitTokenKind = TokenKind::LetValueDecl;
        carriedEmitTokenData = packData1(TokenKind::LetValueDecl, this_identifier);
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState var_parameter
var_parameter$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::VarParameter;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED var_parameter$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::VarValueDecl, this_identifier
        carriedEmitTokenKind = TokenKind::VarValueDecl;
        carriedEmitTokenData = packData1(TokenKind::VarValueDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState impl_expression
impl_expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
impl_expression$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::ImplExpression;
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // pushScope ScopeKind::ParenInImplExpr
        scopePosition = pushScope(scopePosition, ScopeKind::ParenInImplExpr);
        // emitToken TokenKind::ParenthesizedExpr
        carriedEmitTokenKind = TokenKind::ParenthesizedExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED impl_expression$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::IdentifierExpr, this_identifier
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next after_impl_expression
        goto after_impl_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_impl_expression
after_impl_expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_impl_expression$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterImplExpression;
    if (std::string_view(tokEnd, 2) == "::"sv) {
        tokEnd += 2;
        // next impl_access_expression
        goto impl_access_expression$no_emit;
    }
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::BraceInImplExpr
        scopePosition = pushScope(scopePosition, ScopeKind::BraceInImplExpr);
        // emitCallToken TokenKind::Parameterize
        argumentPosition = emitCallToken(argumentPosition, TokenKind::Parameterize, tokBegin, state);
        // next argument
        goto argument$no_emit;
    }
    if (std::string_view(tokEnd, 1) == ">"sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // popScope ScopeKind::GenericCategoryExpression
            {
                auto result = popScope(scopePosition, ScopeKind::GenericCategoryExpression);
                if (result == nullptr) {
                    errorToken = LexerToken::Greater;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // next after_variable_modifier
            goto after_variable_modifier$no_emit;
        }
    }
    // ifScope ScopeKind::StructImplExpression
    if (scopePosition[0] == ScopeKind::StructImplExpression) {
        // popScope ScopeKind::StructImplExpression
        {
            auto result = popScope(scopePosition, ScopeKind::StructImplExpression);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // then after_struct_declaration_id
        goto after_struct_declaration_id$as_then;
    }
    // ifScope ScopeKind::FunctionImplExpression
    if (scopePosition[0] == ScopeKind::FunctionImplExpression) {
        // popScope ScopeKind::FunctionImplExpression
        {
            auto result = popScope(scopePosition, ScopeKind::FunctionImplExpression);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // then after_function_declaration_id
        goto after_function_declaration_id$as_then;
    }
    // ifScope ScopeKind::EnumImplExpression
    if (scopePosition[0] == ScopeKind::EnumImplExpression) {
        // popScope ScopeKind::EnumImplExpression
        {
            auto result = popScope(scopePosition, ScopeKind::EnumImplExpression);
            if (result == nullptr) {
                goto error$as_then;
            }
            scopePosition = result;
        }
        // then after_enum_declaration_id
        goto after_enum_declaration_id$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState impl_access_expression
impl_access_expression$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::ImplAccessExpression;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED impl_access_expression$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::StaticAccessExpr, this_identifier
        carriedEmitTokenKind = TokenKind::StaticAccessExpr;
        carriedEmitTokenData = packData1(TokenKind::StaticAccessExpr, this_identifier);
        // next after_impl_expression
        goto after_impl_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState no_declaration
no_declaration$as_then:
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Namespace, ScopeKind::Struct, ScopeKind::Enum
        {
            auto result = popScope(scopePosition, ScopeKind::Namespace, ScopeKind::Struct, ScopeKind::Enum);
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
        if (sema::isKeyword(this_identifier)) {
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
    LABEL_MAYBE_UNUSED namespace_declaration$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["namespace"]) {
                // next namespace_declaration_id
                goto namespace_declaration_id$no_emit;
            }
            if (this_identifier == words["template"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next after_template
                goto after_template$no_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // -> templated_declaration
        goto templated_declaration$identifier_case;
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
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED namespace_declaration_id$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // rememberDeclarationBegin
        declarationBegin = state.parseOutput.currentToken();
        // commitDeclaration DeclarationKind::Namespace, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Namespace>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::NamespaceDecl, this_declaration
        carriedEmitTokenKind = TokenKind::NamespaceDecl;
        carriedEmitTokenData = packData1(TokenKind::NamespaceDecl, this_declaration);
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
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED templated_declaration$keyword_check:
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
    LABEL_MAYBE_UNUSED templated_declaration$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
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
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // -> no_declaration
        // -> error
        goto error$identifier_case;
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
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED templated_declaration_with_attributes$keyword_check:
            if (this_identifier == words["static"]) {
                // next after_static
                goto after_static$no_emit;
            }
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED templated_declaration_with_attributes$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
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
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // -> error
        goto error$identifier_case;
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
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED function_declaration_id$keyword_check:
            if (this_identifier == words["impl"]) {
                // commitImplDeclaration DeclarationKind::Function
                this_declaration = commitImplDeclaration<DeclarationKind::Function>(tokBegin, declarationBegin, state);
                // pushScope ScopeKind::FunctionImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::FunctionImplExpression);
                // emitToken TokenKind::FunctionImplDecl, this_declaration
                carriedEmitTokenKind = TokenKind::FunctionImplDecl;
                carriedEmitTokenData = packData1(TokenKind::FunctionImplDecl, this_declaration);
                // next impl_expression
                goto impl_expression$with_emit;
            }
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED function_declaration_id$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::Function, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Function>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::FunctionDecl, this_declaration
        carriedEmitTokenKind = TokenKind::FunctionDecl;
        carriedEmitTokenData = packData1(TokenKind::FunctionDecl, this_declaration);
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
after_function_declaration_id$as_then:
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

    // LinearState struct_declaration_id
struct_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::StructDeclarationId;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED struct_declaration_id$keyword_check:
            if (this_identifier == words["impl"]) {
                // commitImplDeclaration DeclarationKind::Struct
                this_declaration = commitImplDeclaration<DeclarationKind::Struct>(tokBegin, declarationBegin, state);
                // pushScope ScopeKind::StructImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::StructImplExpression);
                // emitToken TokenKind::StructImplDecl, this_declaration
                carriedEmitTokenKind = TokenKind::StructImplDecl;
                carriedEmitTokenData = packData1(TokenKind::StructImplDecl, this_declaration);
                // next impl_expression
                goto impl_expression$with_emit;
            }
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED struct_declaration_id$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::Struct, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Struct>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::StructDecl, this_declaration
        carriedEmitTokenKind = TokenKind::StructDecl;
        carriedEmitTokenData = packData1(TokenKind::StructDecl, this_declaration);
        // next after_struct_declaration_id
        goto after_struct_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_struct_declaration_id
after_struct_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_struct_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterStructDeclarationId;
after_struct_declaration_id$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // next struct_declaration_body
            goto struct_declaration_body$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState struct_declaration_body
struct_declaration_body$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::StructDeclarationBody;
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::Struct
        scopePosition = pushScope(scopePosition, ScopeKind::Struct);
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
        if (sema::isKeyword(this_identifier)) {
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
    LABEL_MAYBE_UNUSED member_declaration$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["has"]) {
                // pushScope ScopeKind::HasTypeExpr
                scopePosition = pushScope(scopePosition, ScopeKind::HasTypeExpr);
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // commitDeclaration DeclarationKind::HasMember
                this_declaration = commitDeclaration<DeclarationKind::HasMember>(Word(), tokBegin, declarationBegin, state);
                // emitToken TokenKind::HasMemberDecl, this_declaration
                carriedEmitTokenKind = TokenKind::HasMemberDecl;
                carriedEmitTokenData = packData1(TokenKind::HasMemberDecl, this_declaration);
                // next expression
                goto expression$with_emit;
            }
            if (this_identifier == words["template"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next after_template
                goto after_template$no_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // rememberDeclarationBegin
        declarationBegin = state.parseOutput.currentToken();
        // commitDeclaration DeclarationKind::Member, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Member>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::MemberDecl, this_declaration
        carriedEmitTokenKind = TokenKind::MemberDecl;
        carriedEmitTokenData = packData1(TokenKind::MemberDecl, this_declaration);
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState enum_declaration_id
enum_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::EnumDeclarationId;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED enum_declaration_id$keyword_check:
            if (this_identifier == words["impl"]) {
                // commitImplDeclaration DeclarationKind::Enum
                this_declaration = commitImplDeclaration<DeclarationKind::Enum>(tokBegin, declarationBegin, state);
                // pushScope ScopeKind::EnumImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::EnumImplExpression);
                // emitToken TokenKind::EnumImplDecl, this_declaration
                carriedEmitTokenKind = TokenKind::EnumImplDecl;
                carriedEmitTokenData = packData1(TokenKind::EnumImplDecl, this_declaration);
                // next impl_expression
                goto impl_expression$with_emit;
            }
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED enum_declaration_id$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::Enum, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Enum>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::EnumDecl, this_declaration
        carriedEmitTokenKind = TokenKind::EnumDecl;
        carriedEmitTokenData = packData1(TokenKind::EnumDecl, this_declaration);
        // next after_enum_declaration_id
        goto after_enum_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_enum_declaration_id
after_enum_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_enum_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterEnumDeclarationId;
after_enum_declaration_id$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // next enum_declaration_body
            goto enum_declaration_body$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState enum_declaration_body
enum_declaration_body$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::EnumDeclarationBody;
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::Enum
        scopePosition = pushScope(scopePosition, ScopeKind::Enum);
        // next enum_value_declaration
        goto enum_value_declaration$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState enum_value_declaration
enum_value_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::EnumValueDeclaration;
enum_value_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
    LABEL_MAYBE_UNUSED enum_value_declaration$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["template"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next after_template
                goto after_template$no_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = state.parseOutput.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // rememberDeclarationBegin
        declarationBegin = state.parseOutput.currentToken();
        // commitDeclaration DeclarationKind::EnumValue, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::EnumValue>(this_identifier, tokBegin, declarationBegin, state);
        // emitToken TokenKind::ImplicitEnumValueDecl, this_declaration
        carriedEmitTokenKind = TokenKind::ImplicitEnumValueDecl;
        carriedEmitTokenData = packData1(TokenKind::ImplicitEnumValueDecl, this_declaration);
        // next after_enum_value_declaration_id
        goto after_enum_value_declaration_id$with_emit;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState after_enum_value_declaration_id
after_enum_value_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, state);
after_enum_value_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterEnumValueDeclarationId;
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // updateKind TokenKind::ExplicitEnumValueDecl
            state.parseOutput.tokens.back().setKind(TokenKind::ExplicitEnumValueDecl);
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
        // emitToken TokenKind::ExpressionStmt
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_declaration
        goto after_declaration$with_emit;
    }
    // then error
    goto error$as_then;

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
        if (sema::isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_static$keyword_check:
            if (this_identifier == words["var"]) {
                // next static_var_variable_declaration
                goto static_var_variable_declaration$no_emit;
            }
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED after_static$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["open"]) {
                // next static_open_variable_declaration
                goto static_open_variable_declaration$no_emit;
            }
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, state);
        // setGlobalKind GlobalKind::Let
        setGlobalKind(state, GlobalKind::Let);
        // emitToken TokenKind::GlobalDecl, this_declaration
        carriedEmitTokenKind = TokenKind::GlobalDecl;
        carriedEmitTokenData = packData1(TokenKind::GlobalDecl, this_declaration);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
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
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED static_var_variable_declaration$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, state);
        // setGlobalKind GlobalKind::Var
        setGlobalKind(state, GlobalKind::Var);
        // emitToken TokenKind::GlobalDecl, this_declaration
        carriedEmitTokenKind = TokenKind::GlobalDecl;
        carriedEmitTokenData = packData1(TokenKind::GlobalDecl, this_declaration);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_open_variable_declaration
static_open_variable_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::StaticOpenVariableDeclaration;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (sema::isKeyword(this_identifier)) {
            // -> error
            goto error$keyword_check;
        }
    LABEL_MAYBE_UNUSED static_open_variable_declaration$identifier_case:
        if (sema::isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, state);
        // setGlobalKind GlobalKind::OpenLet
        setGlobalKind(state, GlobalKind::OpenLet);
        // emitToken TokenKind::GlobalDecl, this_declaration
        carriedEmitTokenKind = TokenKind::GlobalDecl;
        carriedEmitTokenData = packData1(TokenKind::GlobalDecl, this_declaration);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
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
    // ifScope ScopeKind::Struct
    if (scopePosition[0] == ScopeKind::Struct) {
        // then member_declaration
        goto member_declaration$as_then;
    }
    // ifScope ScopeKind::Namespace
    if (scopePosition[0] == ScopeKind::Namespace) {
        // then namespace_declaration
        goto namespace_declaration$as_then;
    }
    // ifScope ScopeKind::Enum
    if (scopePosition[0] == ScopeKind::Enum) {
        // then enum_value_declaration
        goto enum_value_declaration$as_then;
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
        goto error$word_case_entry;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
error$word_case_entry:
    {
        auto wordAndPos = readWord(tokEnd, state);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (sema::isKeyword(this_identifier)) {
    LABEL_MAYBE_UNUSED error$keyword_check:
        if (this_identifier == words["assert"]) {
            // error
            errorToken = LexerToken::Assert;
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
        if (this_identifier == words["const"]) {
            // error
            errorToken = LexerToken::Const;
            goto handle_parse_error;
        }
        if (this_identifier == words["continue"]) {
            // error
            errorToken = LexerToken::Continue;
            goto handle_parse_error;
        }
        if (this_identifier == words["destroy"]) {
            // error
            errorToken = LexerToken::Destroy;
            goto handle_parse_error;
        }
        if (this_identifier == words["discard"]) {
            // error
            errorToken = LexerToken::Discard;
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
        if (this_identifier == words["for"]) {
            // error
            errorToken = LexerToken::For;
            goto handle_parse_error;
        }
        if (this_identifier == words["if"]) {
            // error
            errorToken = LexerToken::If;
            goto handle_parse_error;
        }
        if (this_identifier == words["impl"]) {
            // error
            errorToken = LexerToken::Impl;
            goto handle_parse_error;
        }
        if (this_identifier == words["let"]) {
            // error
            errorToken = LexerToken::Let;
            goto handle_parse_error;
        }
        if (this_identifier == words["return"]) {
            // error
            errorToken = LexerToken::Return;
            goto handle_parse_error;
        }
        if (this_identifier == words["shared"]) {
            // error
            errorToken = LexerToken::Shared;
            goto handle_parse_error;
        }
        if (this_identifier == words["static"]) {
            // error
            errorToken = LexerToken::Static;
            goto handle_parse_error;
        }
        if (this_identifier == words["try"]) {
            // error
            errorToken = LexerToken::Try;
            goto handle_parse_error;
        }
        if (this_identifier == words["unique"]) {
            // error
            errorToken = LexerToken::Unique;
            goto handle_parse_error;
        }
        if (this_identifier == words["var"]) {
            // error
            errorToken = LexerToken::Var;
            goto handle_parse_error;
        }
        if (this_identifier == words["while"]) {
            // error
            errorToken = LexerToken::While;
            goto handle_parse_error;
        }
        VERIFY_NOT_REACHED();
    }
LABEL_MAYBE_UNUSED error$identifier_case:
    if (sema::isSpecialIdentifier(this_identifier)) {
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

}