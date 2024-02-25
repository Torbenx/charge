#include "WordTable.h"
#include "parse.h"
#include <utility>

using namespace std::string_view_literals;

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

static SourceLocation locationInCurrentLine(const char* position, ParseState& state) {
    return { (uint32_t)(position - state.lines.back().begin), (uint32_t)state.lines.size() - 1 };
}

[[gnu::noinline]] static void emitNode(NodeKind kind, const char* begin, uint32_t data, ParseState& state) {
    state.nodes.push_back({ kind, locationInCurrentLine(begin, state), data });
}

static void markLineBegin(const char* position, ParseState& state) {
    state.lines.push_back({ position });
}

struct WordAndPosition {
    const char* position;
    Word word;
};
[[nodiscard]] [[gnu::noinline]] static WordAndPosition readWord(const char* position, WordStringTable& wordTable) {
    const char* wordBegin = position;
    Word::HashState hashState;
    do {
        Word::iterateHash(hashState, position[0]);
        position += 1;
    } while (isWordBulkCharacter(position[0]));
    auto hash = Word::finalizeHash(hashState);
    Word word = wordTable.getWithHash(std::string_view(wordBegin, position), hash);
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

[[gnu::noinline]] static void emitWhitespace(WhitespaceKind kind, const char* begin, const char* end, ParseState& state) {
    state.whitespace.push_back({ { kind, locationInCurrentLine(begin, state) }, (uint32_t)(end - begin) });
}

[[nodiscard]] [[gnu::noinline]] static const char* inlineAdvancer(const char* tokEnd, ParseState& state) {
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

void parseExpression(const char* sourceBufferPosition, ParseState& state, ErrorHandler* errorHandler) {
    ScopeBuffer scopeBuffer;
    ScopeKind* scopePosition = scopeBuffer.buffer;
    scopePosition[0] = ScopeKind::Invalid;

    const char* tokBegin = sourceBufferPosition;
    const char* tokEnd = sourceBufferPosition;
    NodeKind carriedEmitNodeKind = (NodeKind)0;
    Word word;
    uint32_t nodeData = 0;

    NodeKind nodeKind = (NodeKind)0;

    scopePosition = pushScope(scopePosition, ScopeKind::Namespace);
    State parseState = State::NamespaceDeclaration;
    Token errorToken = (Token)0;

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
    case State::AfterDeclaration:
        goto after_declaration$no_emit;
    case State::Error:
        VERIFY_NOT_REACHED();
    }
    // SwitchState expression
expression$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
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
            errorToken = Token::ExclaimEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::LogicalNotExpr
        carriedEmitNodeKind = NodeKind::LogicalNotExpr;
        // next expression
        goto expression$with_emit;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::PercentEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Percent;
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
                errorToken = Token::AmpAmpEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::AmpAmp;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::AmpEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Amp;
        goto handle_parse_error;
    }
    case '(': {
        tokEnd += 1;
        // emitNode NodeKind::ParenthesizedExpr
        carriedEmitNodeKind = NodeKind::ParenthesizedExpr;
        // next first_argument_paren
        goto first_argument_paren$with_emit;
    }
    case ')': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::RightParen;
        goto handle_parse_error;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::StarEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::DereferenceExpr
        carriedEmitNodeKind = NodeKind::DereferenceExpr;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitNode NodeKind::PreIncrementExpr
            carriedEmitNodeKind = NodeKind::PreIncrementExpr;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::PlusEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::PlusExpr
        carriedEmitNodeKind = NodeKind::PlusExpr;
        // next expression
        goto expression$with_emit;
    }
    case ',': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Comma;
        goto handle_parse_error;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // emitNode NodeKind::PreDecrementExpr
            carriedEmitNodeKind = NodeKind::PreDecrementExpr;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::MinusEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::MinusGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::NegateExpr
        carriedEmitNodeKind = NodeKind::NegateExpr;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Point;
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
            errorToken = Token::SlashEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Slash;
        goto handle_parse_error;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::ColonColon;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Colon;
        goto handle_parse_error;
    }
    case ';': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::SemiColon;
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
                errorToken = Token::LessLessEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::LessLess;
            goto handle_parse_error;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = Token::LessEqualGreater;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::LessEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Less;
        goto handle_parse_error;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::EqualEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::EqualGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Equal;
        goto handle_parse_error;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::GreaterEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = Token::GreaterGreaterEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::GreaterGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Greater;
        goto handle_parse_error;
    }
    case '[': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::LeftSqure;
        goto handle_parse_error;
    }
    case ']': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::RightSqure;
        goto handle_parse_error;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::HatEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Hat;
        goto handle_parse_error;
    }
    case '{': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::LeftBrace;
        goto handle_parse_error;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::VertEqual;
            goto handle_parse_error;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = Token::VertVertEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::VertVert;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Vert;
        goto handle_parse_error;
    }
    case '}': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::RightBrace;
        goto handle_parse_error;
    }
    case '~': {
        tokEnd += 1;
        // emitNode NodeKind::BitwiseNotExpr
        carriedEmitNodeKind = NodeKind::BitwiseNotExpr;
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
        // emitNode NodeKind::LiteralExpr
        carriedEmitNodeKind = NodeKind::LiteralExpr;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // emitNode NodeKind::LiteralExpr
        carriedEmitNodeKind = NodeKind::LiteralExpr;
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
        auto wordAndPos = readWord(tokEnd, state.wordTable);
        tokEnd = wordAndPos.position;
        word = wordAndPos.word;
    }
    if (word.keyword()) {
    [[maybe_unused]] expression$keyword_check:
        if (word == words["if"]) {
            // pushScope ScopeKind::IfExpr
            scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
            // next expression
            goto expression$no_emit;
        }
        // -> error
        goto error$keyword_check;
    }
    nodeData = word.asUint();
    // emitNode NodeKind::IdentifierExpr
    carriedEmitNodeKind = NodeKind::IdentifierExpr;
    // next after_expression
    goto after_expression$with_emit;

    // SwitchState after_expression
after_expression$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
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
            // emitNode NodeKind::CompareNotEqualExpr
            carriedEmitNodeKind = NodeKind::CompareNotEqualExpr;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Exclaim;
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
                    errorToken = Token::PercentEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::RemainderUpdateStmt
            carriedEmitNodeKind = NodeKind::RemainderUpdateStmt;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::RemainderExpr
        carriedEmitNodeKind = NodeKind::RemainderExpr;
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
                        errorToken = Token::AmpAmpEqual;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitNode NodeKind::LogicalAndUpdateStmt
                carriedEmitNodeKind = NodeKind::LogicalAndUpdateStmt;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitNode NodeKind::LogicalAndExpr
            carriedEmitNodeKind = NodeKind::LogicalAndExpr;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = Token::AmpEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::BitwiseAndUpdateStmt
            carriedEmitNodeKind = NodeKind::BitwiseAndUpdateStmt;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::BitwiseAndExpr
        carriedEmitNodeKind = NodeKind::BitwiseAndExpr;
        // next expression
        goto expression$with_emit;
    }
    case '(': {
        tokEnd += 1;
        // emitNode NodeKind::CallExpr
        carriedEmitNodeKind = NodeKind::CallExpr;
        // next first_argument_paren
        goto first_argument_paren$with_emit;
    }
    case ')': {
        tokEnd += 1;
        // ifScope ScopeKind::RightExpr
        if (scopePosition[0] == ScopeKind::RightExpr) {
            // popScope ScopeKind::RightExpr
            {
                auto result = popScope(scopePosition, ScopeKind::RightExpr);
                if (result == nullptr) {
                    errorToken = Token::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    errorToken = Token::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitNode NodeKind::ExpressionStmt
            carriedEmitNodeKind = NodeKind::ExpressionStmt;
            // next after_parameters
            goto after_parameters$with_emit;
        }
        // ifScope ScopeKind::VariableType
        if (scopePosition[0] == ScopeKind::VariableType) {
            // popScope ScopeKind::VariableType
            {
                auto result = popScope(scopePosition, ScopeKind::VariableType);
                if (result == nullptr) {
                    errorToken = Token::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    errorToken = Token::RightParen;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitNode NodeKind::AssignStmt
            emitNode(NodeKind::AssignStmt, tokBegin, nodeData, state);
            nodeData = 0;
            // emitNode NodeKind::ExpressionStmt
            carriedEmitNodeKind = NodeKind::ExpressionStmt;
            // next after_parameters
            goto after_parameters$with_emit;
        }
        // popScope ScopeKind::Paren
        {
            auto result = popScope(scopePosition, ScopeKind::Paren);
            if (result == nullptr) {
                errorToken = Token::RightParen;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
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
                    errorToken = Token::StarEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::MultiplyUpdateStmt
            carriedEmitNodeKind = NodeKind::MultiplyUpdateStmt;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::MultiplyExpr
        carriedEmitNodeKind = NodeKind::MultiplyExpr;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitNode NodeKind::PostIncrementExpr
            carriedEmitNodeKind = NodeKind::PostIncrementExpr;
            // next after_expression
            goto after_expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = Token::PlusEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::AdditionUpdateStmt
            carriedEmitNodeKind = NodeKind::AdditionUpdateStmt;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::AdditionExpr
        carriedEmitNodeKind = NodeKind::AdditionExpr;
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
            // emitNode NodeKind::PostDecrementExpr
            carriedEmitNodeKind = NodeKind::PostDecrementExpr;
            // next after_expression
            goto after_expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = Token::MinusEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::SubtractionUpdateStmt
            carriedEmitNodeKind = NodeKind::SubtractionUpdateStmt;
            // next expression
            goto expression$with_emit;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            // error
            errorToken = Token::MinusGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::SubtractionExpr
        carriedEmitNodeKind = NodeKind::SubtractionExpr;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // nodeKind = NodeKind::MemberAccessExpr
        nodeKind = NodeKind::MemberAccessExpr;
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
                    errorToken = Token::SlashEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::DivideUpdateStmt
            carriedEmitNodeKind = NodeKind::DivideUpdateStmt;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::DivideExpr
        carriedEmitNodeKind = NodeKind::DivideExpr;
        // next expression
        goto expression$with_emit;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // nodeKind = NodeKind::StaticAccessExpr
            nodeKind = NodeKind::StaticAccessExpr;
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
                    errorToken = Token::Colon;
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
                    errorToken = Token::Colon;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::FunctionBody
            scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
            // next single_or_compound_statement
            goto single_or_compound_statement$no_emit;
        }
        // popScope ScopeKind::IfExprOrStmt
        {
            auto result = popScope(scopePosition, ScopeKind::IfExprOrStmt);
            if (result == nullptr) {
                errorToken = Token::Colon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::LeftExpr
        {
            auto result = popScope(scopePosition, ScopeKind::LeftExpr);
            if (result == nullptr) {
                errorToken = Token::Colon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::PlainStatement
        {
            auto result = popScope(scopePosition, ScopeKind::PlainStatement);
            if (result == nullptr) {
                errorToken = Token::Colon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::IfBranch
        scopePosition = pushScope(scopePosition, ScopeKind::IfBranch);
        // emitNode NodeKind::IfStmt
        carriedEmitNodeKind = NodeKind::IfStmt;
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
                    errorToken = Token::SemiColon;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_declaration
            goto after_declaration$with_emit;
        }
        // ifScope ScopeKind::VariableType
        if (scopePosition[0] == ScopeKind::VariableType) {
            // popScope ScopeKind::VariableType
            {
                auto result = popScope(scopePosition, ScopeKind::VariableType);
                if (result == nullptr) {
                    errorToken = Token::SemiColon;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::AssignStmt
            emitNode(NodeKind::AssignStmt, tokBegin, nodeData, state);
            nodeData = 0;
        }
        // popScope ScopeKind::LeftExpr, ScopeKind::RightExpr
        {
            auto result = popScope(scopePosition, ScopeKind::LeftExpr, ScopeKind::RightExpr);
            if (result == nullptr) {
                errorToken = Token::SemiColon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::ExpressionStmt
        carriedEmitNodeKind = NodeKind::ExpressionStmt;
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
                        errorToken = Token::LessLessEqual;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitNode NodeKind::ShiftLeftUpdateStmt
                carriedEmitNodeKind = NodeKind::ShiftLeftUpdateStmt;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitNode NodeKind::ShiftLeftExpr
            carriedEmitNodeKind = NodeKind::ShiftLeftExpr;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // -> error
                // error
                errorToken = Token::LessEqualGreater;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // emitNode NodeKind::CompareLessEqualExpr
            carriedEmitNodeKind = NodeKind::CompareLessEqualExpr;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::CompareLessExpr
        carriedEmitNodeKind = NodeKind::CompareLessExpr;
        // next expression
        goto expression$with_emit;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitNode NodeKind::CompareEqualExpr
            carriedEmitNodeKind = NodeKind::CompareEqualExpr;
            // next expression
            goto expression$with_emit;
        }
        if (next == '>') {
            tokEnd += 2;
            // popScope ScopeKind::IfExpr, ScopeKind::IfExprOrStmt
            {
                auto result = popScope(scopePosition, ScopeKind::IfExpr, ScopeKind::IfExprOrStmt);
                if (result == nullptr) {
                    errorToken = Token::EqualGreater;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // emitNode NodeKind::IfExpr
            carriedEmitNodeKind = NodeKind::IfExpr;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // popScope ScopeKind::VariableType, ScopeKind::LeftExpr
        {
            auto result = popScope(scopePosition, ScopeKind::VariableType, ScopeKind::LeftExpr);
            if (result == nullptr) {
                errorToken = Token::Equal;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // emitNode NodeKind::AssignStmt
        carriedEmitNodeKind = NodeKind::AssignStmt;
        // next expression
        goto expression$with_emit;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitNode NodeKind::CompareGreaterEqualExpr
            carriedEmitNodeKind = NodeKind::CompareGreaterEqualExpr;
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
                        errorToken = Token::GreaterGreaterEqual;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitNode NodeKind::ShiftRightUpdateStmt
                carriedEmitNodeKind = NodeKind::ShiftRightUpdateStmt;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitNode NodeKind::ShiftRightExpr
            carriedEmitNodeKind = NodeKind::ShiftRightExpr;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::CompareGreaterExpr
        carriedEmitNodeKind = NodeKind::CompareGreaterExpr;
        // next expression
        goto expression$with_emit;
    }
    case '[': {
        tokEnd += 1;
        // emitNode NodeKind::IndexExpr
        carriedEmitNodeKind = NodeKind::IndexExpr;
        // next first_argument_square
        goto first_argument_square$with_emit;
    }
    case ']': {
        tokEnd += 1;
        // popScope ScopeKind::Square
        {
            auto result = popScope(scopePosition, ScopeKind::Square);
            if (result == nullptr) {
                errorToken = Token::RightSqure;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
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
                    errorToken = Token::HatEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::BitwiseXorUpdateStmt
            carriedEmitNodeKind = NodeKind::BitwiseXorUpdateStmt;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::BitwiseXorExpr
        carriedEmitNodeKind = NodeKind::BitwiseXorExpr;
        // next expression
        goto expression$with_emit;
    }
    case '{': {
        tokEnd += 1;
        // emitNode NodeKind::Parameterize
        carriedEmitNodeKind = NodeKind::Parameterize;
        // next first_argument_brace
        goto first_argument_brace$with_emit;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    errorToken = Token::VertEqual;
                    goto handle_parse_error;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitNode NodeKind::BitwiseOrUpdateStmt
            carriedEmitNodeKind = NodeKind::BitwiseOrUpdateStmt;
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
                        errorToken = Token::VertVertEqual;
                        goto handle_parse_error;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitNode NodeKind::LogicalOrUpdateStmt
                carriedEmitNodeKind = NodeKind::LogicalOrUpdateStmt;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitNode NodeKind::LogicalOrExpr
            carriedEmitNodeKind = NodeKind::LogicalOrExpr;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::BitwiseOrExpr
        carriedEmitNodeKind = NodeKind::BitwiseOrExpr;
        // next expression
        goto expression$with_emit;
    }
    case '}': {
        tokEnd += 1;
        // popScope ScopeKind::Brace
        {
            auto result = popScope(scopePosition, ScopeKind::Brace);
            if (result == nullptr) {
                errorToken = Token::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '~': {
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Tilde;
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
        errorToken = Token::Literal;
        goto handle_parse_error;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // -> error
        // error
        errorToken = Token::Literal;
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
        auto wordAndPos = readWord(tokEnd, state.wordTable);
        tokEnd = wordAndPos.position;
        word = wordAndPos.word;
    }
    if (word.keyword()) {
        // -> error
        goto error$keyword_check;
    }
    nodeData = word.asUint();
    // -> error
    // error
    errorToken = Token::Identifier;
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
                errorToken = Token::RightParen;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (std::string_view(tokEnd, 1) == "]"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Square
        {
            auto result = popScope(scopePosition, ScopeKind::Square);
            if (result == nullptr) {
                errorToken = Token::RightSqure;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Brace
        {
            auto result = popScope(scopePosition, ScopeKind::Brace);
            if (result == nullptr) {
                errorToken = Token::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] comma_after_expression$keyword_check:
            if (word == words["else"]) {
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
                // emitNode NodeKind::AssignStmt
                emitNode(NodeKind::AssignStmt, tokBegin, nodeData, state);
                nodeData = 0;
                // emitNode NodeKind::ExpressionStmt
                emitNode(NodeKind::ExpressionStmt, tokBegin, nodeData, state);
                nodeData = 0;
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
                // emitNode NodeKind::ExpressionStmt
                emitNode(NodeKind::ExpressionStmt, tokBegin, nodeData, state);
                nodeData = 0;
                // then parameter
                goto parameter$keyword_check;
            }
            // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace
            if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace) {
                // then check_designated_argument
                // -> expression
                goto expression$keyword_check;
            }
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // ifScope ScopeKind::Parameter
        if (scopePosition[0] == ScopeKind::Parameter) {
            // then parameter
            // nodeKind = NodeKind::LetParameter
            nodeKind = NodeKind::LetParameter;
            // -> variable_declaration
            // emitNode nodeKind
            carriedEmitNodeKind = nodeKind;
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
            // emitNode NodeKind::AssignStmt
            emitNode(NodeKind::AssignStmt, tokBegin, nodeData, state);
            nodeData = 0;
            // emitNode NodeKind::ExpressionStmt
            emitNode(NodeKind::ExpressionStmt, tokBegin, nodeData, state);
            nodeData = 0;
            // then parameter
            // nodeKind = NodeKind::LetParameter
            nodeKind = NodeKind::LetParameter;
            // -> variable_declaration
            // emitNode nodeKind
            carriedEmitNodeKind = nodeKind;
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
            // emitNode NodeKind::ExpressionStmt
            emitNode(NodeKind::ExpressionStmt, tokBegin, nodeData, state);
            nodeData = 0;
            // then parameter
            // nodeKind = NodeKind::LetParameter
            nodeKind = NodeKind::LetParameter;
            // -> variable_declaration
            // emitNode nodeKind
            carriedEmitNodeKind = nodeKind;
            // next after_variable_declaration_id
            goto after_variable_declaration_id$with_emit;
        }
        // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace
        if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace) {
            // then check_designated_argument
            // emitNode NodeKind::IdentifierExpr
            carriedEmitNodeKind = NodeKind::IdentifierExpr;
            // next maybe_designated_argument
            goto maybe_designated_argument$with_emit;
        }
        // -> error
        // error
        errorToken = Token::Identifier;
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
        // emitNode NodeKind::AssignStmt
        emitNode(NodeKind::AssignStmt, tokBegin, nodeData, state);
        nodeData = 0;
        // emitNode NodeKind::ExpressionStmt
        emitNode(NodeKind::ExpressionStmt, tokBegin, nodeData, state);
        nodeData = 0;
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
        // emitNode NodeKind::ExpressionStmt
        emitNode(NodeKind::ExpressionStmt, tokBegin, nodeData, state);
        nodeData = 0;
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
        // emitNode NodeKind::CommaElseExpr
        carriedEmitNodeKind = NodeKind::CommaElseExpr;
        // next expression
        goto expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState check_designated_argument
check_designated_argument$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
            // -> expression
            goto expression$keyword_check;
        }
        nodeData = word.asUint();
        // emitNode NodeKind::IdentifierExpr
        carriedEmitNodeKind = NodeKind::IdentifierExpr;
        // next maybe_designated_argument
        goto maybe_designated_argument$with_emit;
    }
    // then expression
    goto expression$as_then;

    // LinearState maybe_designated_argument
maybe_designated_argument$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
maybe_designated_argument$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::MaybeDesignatedArgument;
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // updateKind NodeKind::DesignateArgument
            state.nodes.back().setKind(NodeKind::DesignateArgument);
            // next expression
            goto expression$no_emit;
        }
    }
    // then after_expression
    goto after_expression$as_then;

    // LinearState first_argument_paren
first_argument_paren$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
first_argument_paren$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentParen;
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::Paren
    scopePosition = pushScope(scopePosition, ScopeKind::Paren);
    // then check_designated_argument
    goto check_designated_argument$as_then;

    // LinearState first_argument_square
first_argument_square$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
first_argument_square$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentSquare;
    if (std::string_view(tokEnd, 1) == "]"sv) {
        tokEnd += 1;
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::Square
    scopePosition = pushScope(scopePosition, ScopeKind::Square);
    // then check_designated_argument
    goto check_designated_argument$as_then;

    // LinearState first_argument_brace
first_argument_brace$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
first_argument_brace$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentBrace;
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // emitNode nodeKind
        carriedEmitNodeKind = nodeKind;
        // next after_expression
        goto after_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState single_or_compound_statement
single_or_compound_statement$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
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
        // emitNode NodeKind::CompoundStmt
        carriedEmitNodeKind = NodeKind::CompoundStmt;
        // next statement
        goto statement$with_emit;
    }
    // then statement
    goto statement$as_then;

    // LinearState after_statement
after_statement$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
after_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterStatement;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] after_statement$keyword_check:
            if (word == words["else"]) {
                // popScope ScopeKind::IfBranch
                {
                    auto result = popScope(scopePosition, ScopeKind::IfBranch);
                    if (result == nullptr) {
                        errorToken = Token::Else;
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
        nodeData = word.asUint();
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
            // ifScope ScopeKind::Type
            if (scopePosition[0] == ScopeKind::Type) {
                // then member_declaration
                // emitNode NodeKind::MemberDecl
                carriedEmitNodeKind = NodeKind::MemberDecl;
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
                errorToken = Token::Identifier;
                goto handle_parse_error;
            }
            // -> error
            // error
            errorToken = Token::Identifier;
            goto handle_parse_error;
        }
        // ifScope ScopeKind::Type
        if (scopePosition[0] == ScopeKind::Type) {
            // then member_declaration
            // emitNode NodeKind::MemberDecl
            carriedEmitNodeKind = NodeKind::MemberDecl;
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
            errorToken = Token::Identifier;
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
        // emitNode NodeKind::IdentifierExpr
        carriedEmitNodeKind = NodeKind::IdentifierExpr;
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

    // LinearState statement
statement$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::Statement;
statement$as_then:
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement
        {
            auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch, ScopeKind::PlainStatement);
            if (result == nullptr) {
                errorToken = Token::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::CompoundStmt
        {
            auto result = popScope(scopePosition, ScopeKind::CompoundStmt);
            if (result == nullptr) {
                errorToken = Token::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_statement
        goto after_statement$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] statement$keyword_check:
            if (word == words["if"]) {
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // pushScope ScopeKind::IfExprOrStmt
                scopePosition = pushScope(scopePosition, ScopeKind::IfExprOrStmt);
                // next expression
                goto expression$no_emit;
            }
            if (word == words["let"]) {
                // next check_var_after_let
                goto check_var_after_let$no_emit;
            }
            if (word == words["var"]) {
                // nodeKind = NodeKind::VarStmt
                nodeKind = NodeKind::VarStmt;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (word == words["return"]) {
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitNode NodeKind::ReturnStmt
                carriedEmitNodeKind = NodeKind::ReturnStmt;
                // next after_return
                goto after_return$with_emit;
            }
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            goto expression$keyword_check;
        }
        nodeData = word.asUint();
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitNode NodeKind::IdentifierExpr
        carriedEmitNodeKind = NodeKind::IdentifierExpr;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::LeftExpr
    scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
    // then expression
    goto expression$as_then;

    // LinearState after_return
after_return$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
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
                errorToken = Token::SemiColon;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // updateKind NodeKind::EmptyReturnStmt
        state.nodes.back().setKind(NodeKind::EmptyReturnStmt);
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
            // emitNode NodeKind::ElseStmt
            carriedEmitNodeKind = NodeKind::ElseStmt;
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] check_var_after_let$keyword_check:
            if (word == words["var"]) {
                // nodeKind = NodeKind::VarStmt
                nodeKind = NodeKind::VarStmt;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            // nodeKind = NodeKind::LetStmt
            nodeKind = NodeKind::LetStmt;
            // -> variable_declaration
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // nodeKind = NodeKind::LetStmt
        nodeKind = NodeKind::LetStmt;
        // -> variable_declaration
        // emitNode nodeKind
        carriedEmitNodeKind = nodeKind;
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // nodeKind = NodeKind::LetStmt
    nodeKind = NodeKind::LetStmt;
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // emitNode nodeKind
        carriedEmitNodeKind = nodeKind;
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_variable_declaration_id
after_variable_declaration_id$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
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
            // emitNode NodeKind::AssignStmt
            carriedEmitNodeKind = NodeKind::AssignStmt;
            // next expression
            goto expression$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // emitNode NodeKind::AssignStmt
        emitNode(NodeKind::AssignStmt, tokBegin, nodeData, state);
        nodeData = 0;
        // emitNode NodeKind::ExpressionStmt
        carriedEmitNodeKind = NodeKind::ExpressionStmt;
        // next after_statement
        goto after_statement$with_emit;
    }
    if (std::string_view(tokEnd, 1) == ","sv) {
        tokEnd += 1;
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                errorToken = Token::Comma;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::Parameter
        scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
        // emitNode NodeKind::AssignStmt
        emitNode(NodeKind::AssignStmt, tokBegin, nodeData, state);
        nodeData = 0;
        // emitNode NodeKind::ExpressionStmt
        carriedEmitNodeKind = NodeKind::ExpressionStmt;
        // next parameter
        goto parameter$with_emit;
    }
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                errorToken = Token::RightParen;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // emitNode NodeKind::AssignStmt
        emitNode(NodeKind::AssignStmt, tokBegin, nodeData, state);
        nodeData = 0;
        // emitNode NodeKind::ExpressionStmt
        carriedEmitNodeKind = NodeKind::ExpressionStmt;
        // next after_parameters
        goto after_parameters$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_parameters
after_parameters$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
after_parameters$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterParameters;
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
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
parameter$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::Parameter;
parameter$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] parameter$keyword_check:
            if (word == words["in"]) {
                // nodeKind = NodeKind::InParameter
                nodeKind = NodeKind::InParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (word == words["inout"]) {
                // nodeKind = NodeKind::InOutParameter
                nodeKind = NodeKind::InOutParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (word == words["out"]) {
                // nodeKind = NodeKind::OutParameter
                nodeKind = NodeKind::OutParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (word == words["let"]) {
                // nodeKind = NodeKind::LetParameter
                nodeKind = NodeKind::LetParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (word == words["var"]) {
                // nodeKind = NodeKind::VarParameter
                nodeKind = NodeKind::VarParameter;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            // nodeKind = NodeKind::LetParameter
            nodeKind = NodeKind::LetParameter;
            // -> variable_declaration
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // nodeKind = NodeKind::LetParameter
        nodeKind = NodeKind::LetParameter;
        // -> variable_declaration
        // emitNode nodeKind
        carriedEmitNodeKind = nodeKind;
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // nodeKind = NodeKind::LetParameter
    nodeKind = NodeKind::LetParameter;
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
                errorToken = Token::RightBrace;
                goto handle_parse_error;
            }
            scopePosition = result;
        }
        // next after_declaration
        goto after_declaration$no_emit;
    }
    if (tokEnd[0] == '\0') {
        return;
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] namespace_declaration$keyword_check:
            if (word == words["namespace"]) {
                // next namespace_declaration_id
                goto namespace_declaration_id$no_emit;
            }
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
        nodeData = word.asUint();
        // -> templated_declaration
        // -> no_declaration
        // -> error
        // error
        errorToken = Token::Identifier;
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // next after_namespace_declaration_id
        goto after_namespace_declaration_id$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_namespace_declaration_id
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] templated_declaration$keyword_check:
            if (word == words["template"]) {
                // next after_template
                goto after_template$no_emit;
            }
            if (word == words["fn"]) {
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (word == words["struct"]) {
                // nodeKind = NodeKind::StructTypeDecl
                nodeKind = NodeKind::StructTypeDecl;
                // next type_declaration_id
                goto type_declaration_id$no_emit;
            }
            if (word == words["object"]) {
                // nodeKind = NodeKind::ObjectTypeDecl
                nodeKind = NodeKind::ObjectTypeDecl;
                // next type_declaration_id
                goto type_declaration_id$no_emit;
            }
            if (word == words["static"]) {
                // next after_static
                goto after_static$no_emit;
            }
            // -> no_declaration
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // -> no_declaration
        // -> error
        // error
        errorToken = Token::Identifier;
        goto handle_parse_error;
    }
    // then no_declaration
    goto no_declaration$as_then;

    // LinearState after_template
after_template$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterTemplate;
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // pushScope ScopeKind::TemplateParameters
        scopePosition = pushScope(scopePosition, ScopeKind::TemplateParameters);
        // next first_parameter
        goto first_parameter$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_template_parameters
after_template_parameters$as_then:
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState function_declaration_id
function_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::FunctionDeclarationId;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // emitNode NodeKind::FunctionDecl
        carriedEmitNodeKind = NodeKind::FunctionDecl;
        // next after_function_declaration_id
        goto after_function_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_function_declaration_id
after_function_declaration_id$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
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
            // next single_or_compound_statement
            goto single_or_compound_statement$no_emit;
        }
    }
    if (std::string_view(tokEnd, 2) == "->"sv) {
        tokEnd += 2;
        // pushScope ScopeKind::ReturnType
        scopePosition = pushScope(scopePosition, ScopeKind::ReturnType);
        // next expression
        goto expression$no_emit;
    }
    if (std::string_view(tokEnd, 2) == "=>"sv) {
        tokEnd += 2;
        // pushScope ScopeKind::FunctionBody
        scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // next expression
        goto expression$no_emit;
    }
    if (std::string_view(tokEnd, 3) == "<=>"sv) {
        tokEnd += 3;
        // pushScope ScopeKind::FunctionBody
        scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // next expression
        goto expression$no_emit;
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // emitNode nodeKind
        carriedEmitNodeKind = nodeKind;
        // next after_type_declaration_id
        goto after_type_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_type_declaration_id
after_type_declaration_id$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] member_declaration$keyword_check:
            if (word == words["has"]) {
                // pushScope ScopeKind::HasTypeExpr
                scopePosition = pushScope(scopePosition, ScopeKind::HasTypeExpr);
                // emitNode NodeKind::HasMemberDecl
                carriedEmitNodeKind = NodeKind::HasMemberDecl;
                // next expression
                goto expression$with_emit;
            }
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
        nodeData = word.asUint();
        // emitNode NodeKind::MemberDecl
        carriedEmitNodeKind = NodeKind::MemberDecl;
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
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] after_static$keyword_check:
            if (word == words["var"]) {
                // nodeKind = NodeKind::StaticVarDecl
                nodeKind = NodeKind::StaticVarDecl;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            if (word == words["let"]) {
                // nodeKind = NodeKind::StaticLetDecl
                nodeKind = NodeKind::StaticLetDecl;
                // next variable_declaration
                goto variable_declaration$no_emit;
            }
            // -> error
            goto error$keyword_check;
        }
        nodeData = word.asUint();
        // emitNode NodeKind::StaticLetDecl
        carriedEmitNodeKind = NodeKind::StaticLetDecl;
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_declaration
after_declaration$with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, nodeData, state);
    nodeData = 0;
after_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, state);
    tokBegin = tokEnd;
    parseState = State::AfterDeclaration;
after_declaration$as_then:
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
            errorToken = Token::ExclaimEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Exclaim;
        goto handle_parse_error;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::PercentEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Percent;
        goto handle_parse_error;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                errorToken = Token::AmpAmpEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = Token::AmpAmp;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::AmpEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Amp;
        goto handle_parse_error;
    }
    case '(': {
        tokEnd += 1;
        // error
        errorToken = Token::LeftParen;
        goto handle_parse_error;
    }
    case ')': {
        tokEnd += 1;
        // error
        errorToken = Token::RightParen;
        goto handle_parse_error;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::StarEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Star;
        goto handle_parse_error;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // error
            errorToken = Token::PlusPlus;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::PlusEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Plus;
        goto handle_parse_error;
    }
    case ',': {
        tokEnd += 1;
        // error
        errorToken = Token::Comma;
        goto handle_parse_error;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // error
            errorToken = Token::MinusMinus;
            goto handle_parse_error;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::MinusEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            errorToken = Token::MinusGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Minus;
        goto handle_parse_error;
    }
    case '.': {
        tokEnd += 1;
        // error
        errorToken = Token::Point;
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
            errorToken = Token::SlashEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Slash;
        goto handle_parse_error;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // error
            errorToken = Token::ColonColon;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Colon;
        goto handle_parse_error;
    }
    case ';': {
        tokEnd += 1;
        // error
        errorToken = Token::SemiColon;
        goto handle_parse_error;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                errorToken = Token::LessLessEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = Token::LessLess;
            goto handle_parse_error;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // error
                errorToken = Token::LessEqualGreater;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = Token::LessEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Less;
        goto handle_parse_error;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::EqualEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            errorToken = Token::EqualGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Equal;
        goto handle_parse_error;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::GreaterEqual;
            goto handle_parse_error;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                errorToken = Token::GreaterGreaterEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = Token::GreaterGreater;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Greater;
        goto handle_parse_error;
    }
    case '[': {
        tokEnd += 1;
        // error
        errorToken = Token::LeftSqure;
        goto handle_parse_error;
    }
    case ']': {
        tokEnd += 1;
        // error
        errorToken = Token::RightSqure;
        goto handle_parse_error;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::HatEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Hat;
        goto handle_parse_error;
    }
    case '{': {
        tokEnd += 1;
        // error
        errorToken = Token::LeftBrace;
        goto handle_parse_error;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::VertEqual;
            goto handle_parse_error;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                errorToken = Token::VertVertEqual;
                goto handle_parse_error;
            }
            tokEnd += 2;
            // error
            errorToken = Token::VertVert;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // error
        errorToken = Token::Vert;
        goto handle_parse_error;
    }
    case '}': {
        tokEnd += 1;
        // error
        errorToken = Token::RightBrace;
        goto handle_parse_error;
    }
    case '~': {
        tokEnd += 1;
        // error
        errorToken = Token::Tilde;
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
        errorToken = Token::Literal;
        goto handle_parse_error;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // error
        errorToken = Token::Literal;
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
        auto wordAndPos = readWord(tokEnd, state.wordTable);
        tokEnd = wordAndPos.position;
        word = wordAndPos.word;
    }
    if (word.keyword()) {
    [[maybe_unused]] error$keyword_check:
        if (word == words["if"]) {
            // error
            errorToken = Token::If;
            goto handle_parse_error;
        }
        if (word == words["elif"]) {
            // error
            errorToken = Token::Elif;
            goto handle_parse_error;
        }
        if (word == words["else"]) {
            // error
            errorToken = Token::Else;
            goto handle_parse_error;
        }
        if (word == words["match"]) {
            // error
            errorToken = Token::Match;
            goto handle_parse_error;
        }
        if (word == words["for"]) {
            // error
            errorToken = Token::For;
            goto handle_parse_error;
        }
        if (word == words["while"]) {
            // error
            errorToken = Token::While;
            goto handle_parse_error;
        }
        if (word == words["do"]) {
            // error
            errorToken = Token::Do;
            goto handle_parse_error;
        }
        if (word == words["return"]) {
            // error
            errorToken = Token::Return;
            goto handle_parse_error;
        }
        if (word == words["break"]) {
            // error
            errorToken = Token::Break;
            goto handle_parse_error;
        }
        if (word == words["continue"]) {
            // error
            errorToken = Token::Continue;
            goto handle_parse_error;
        }
        if (word == words["loop"]) {
            // error
            errorToken = Token::Loop;
            goto handle_parse_error;
        }
        if (word == words["guard"]) {
            // error
            errorToken = Token::Guard;
            goto handle_parse_error;
        }
        if (word == words["try"]) {
            // error
            errorToken = Token::Try;
            goto handle_parse_error;
        }
        if (word == words["catch"]) {
            // error
            errorToken = Token::Catch;
            goto handle_parse_error;
        }
        if (word == words["with"]) {
            // error
            errorToken = Token::With;
            goto handle_parse_error;
        }
        if (word == words["analysis"]) {
            // error
            errorToken = Token::Analysis;
            goto handle_parse_error;
        }
        if (word == words["assert"]) {
            // error
            errorToken = Token::Assert;
            goto handle_parse_error;
        }
        if (word == words["namespace"]) {
            // error
            errorToken = Token::Namespace;
            goto handle_parse_error;
        }
        if (word == words["struct"]) {
            // error
            errorToken = Token::Struct;
            goto handle_parse_error;
        }
        if (word == words["trait"]) {
            // error
            errorToken = Token::Trait;
            goto handle_parse_error;
        }
        if (word == words["object"]) {
            // error
            errorToken = Token::Object;
            goto handle_parse_error;
        }
        if (word == words["fn"]) {
            // error
            errorToken = Token::Fn;
            goto handle_parse_error;
        }
        if (word == words["static"]) {
            // error
            errorToken = Token::Static;
            goto handle_parse_error;
        }
        if (word == words["template"]) {
            // error
            errorToken = Token::Template;
            goto handle_parse_error;
        }
        if (word == words["has"]) {
            // error
            errorToken = Token::Has;
            goto handle_parse_error;
        }
        if (word == words["var"]) {
            // error
            errorToken = Token::Var;
            goto handle_parse_error;
        }
        if (word == words["let"]) {
            // error
            errorToken = Token::Let;
            goto handle_parse_error;
        }
        if (word == words["in"]) {
            // error
            errorToken = Token::In;
            goto handle_parse_error;
        }
        if (word == words["inout"]) {
            // error
            errorToken = Token::Inout;
            goto handle_parse_error;
        }
        if (word == words["out"]) {
            // error
            errorToken = Token::Out;
            goto handle_parse_error;
        }
        if (word == words["forward"]) {
            // error
            errorToken = Token::Forward;
            goto handle_parse_error;
        }
        if (word == words["assign"]) {
            // error
            errorToken = Token::Assign;
            goto handle_parse_error;
        }
        VERIFY_NOT_REACHED();
    }
    nodeData = word.asUint();
    // error
    errorToken = Token::Identifier;
    goto handle_parse_error;


handle_parse_error:
    errorHandler->invalidToken(errorToken, parseState, scopePosition, state);
    return;
}

std::string_view nameString(NodeKind kind) {
    switch (kind) {
#define NODE(kind, type, prec) \
    case NodeKind::kind:       \
        return #kind;

#include "nodes.inc"
    }
}

std::string_view nameString(ScopeKind kind) {
    switch (kind) {
#define SCOPE(kind)       \
    case ScopeKind::kind: \
        return #kind;
        ENUMERATE_SCOPE_KINDS
#undef SCOPE
    }
}

}