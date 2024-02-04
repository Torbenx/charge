#include "WordTable.h"
#include "parse.h"
#include <utility>

using namespace std::string_view_literals;

namespace parse {

static bool isWordBulkCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

static bool isWordFirstCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || c == '_' || c == '$';
}

enum class ScopeKind : char {
    Invalid,
    IfExpr,
    IfExprOrStmt,
    CompoundStmt,
    Paren,
    Square,
    Brace,
};

struct ParseStackState {
    std::vector<Node> nodes;
    WordStringTable wordTable { words };
};

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
    auto index = ScopeBuffer::toIndex(position);
    VERIFY(index + 1 < (size_t)SCOPE_BUFFER_SIZE);
    position += 1;
    position[0] = kind;
    return position;
}

template<typename... Args>
static ScopeKind* popScope(ScopeKind* position, Args... kinds) {
    static_assert((std::is_same_v<Args, ScopeKind> && ...));
    VERIFY(((position[0] == kinds) || ...));
    position -= 1;
    return position;
}

static ScopeKind peekScope(ScopeKind* position) {
    return position[0];
}

[[gnu::noinline]] static void emitNode(NodeKind kind, const char* begin, const char* end, ParseStackState& state, const char* sourceBufferBegin) {
    state.nodes.push_back({ kind, (uint32_t)(begin - sourceBufferBegin), (uint32_t)(end - sourceBufferBegin) });
}

struct WordAndPosition {
    const char* position;
    Word word;
};
[[nodiscard]] [[gnu::noinline]] static WordAndPosition readWord(const char* position, WordStringTable& wordTable) {
    const char* wordBegin = position;
    uint32_t hash = 0;
    do {
        hash = Word::iterateHash(hash, position[0]);
        position += 1;
    } while (isWordBulkCharacter(position[0]));
    hash = Word::finalizeHash(hash);
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
    while (position[0] && position[0] != '\'' && position[0] != '\n' && position[0] != '\r') {
        position += 1;
    }
    return position;
};

[[nodiscard]] [[gnu::noinline]] static const char* inlineAdvancer(const char* tokEnd, ParseStackState& state, const char* sourceBufferBegin) {
    const char* tokBegin;
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        tokBegin = tokEnd;
        if (std::string_view(tokEnd, 2) == "//") {
            tokEnd = skipToEndOfLine(tokEnd);
            emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);
            continue;
        }
        if (std::string_view(tokEnd, 2) == "/*") {
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);
            continue;
        }
        if (tokEnd[0] == '\n') {
            tokEnd += 1;
            continue;
        }
        if (tokEnd[0] == '\r') {
            if (tokEnd[1] == '\n') {
                tokEnd += 2;
                continue;
            }
            tokEnd += 1;
            continue;
        }
        break;
    } // retry-loop
    return tokEnd;
}

std::vector<Node> parseExpression(const char* sourceBufferBegin, const char* sourceBufferPosition, ErrorHandler* errorHandler) {
    ScopeBuffer scopeBuffer;
    ScopeKind* scopePosition = scopeBuffer.buffer;
    scopePosition[0] = ScopeKind::Invalid;

    ParseStackState state;
    const char* tokBegin = sourceBufferPosition;
    const char* tokEnd = sourceBufferPosition;
    NodeKind carriedEmitNodeKind = (NodeKind)0;
    Word word;

    const char* savedTokenBegin = nullptr;
    const char* savedTokenEnd = nullptr;
    NodeKind nodeKind = (NodeKind)0;

    State parseState = State::Statement;
    Token errorToken = (Token)0;

    switch (parseState) {
    case State::Expression:
        goto expression_no_emit;
    case State::AfterExpression:
        goto after_expression_no_emit;
    case State::Statement:
        goto statement_no_emit;
    case State::SingleOrCompoundStatement:
        VERIFY_NOT_REACHED();
    case State::CommaAfterExpression:
        VERIFY_NOT_REACHED();
    case State::CommaElse:
        VERIFY_NOT_REACHED();
    case State::CheckDesignatedArgument:
        VERIFY_NOT_REACHED();
    case State::MaybeDesignatedArgument:
        VERIFY_NOT_REACHED();
    case State::FirstArgumentParen:
        goto first_argument_paren_no_emit;
    case State::FirstArgumentSquare:
        VERIFY_NOT_REACHED();
    case State::FirstArgumentBrace:
        VERIFY_NOT_REACHED();
    case State::AccessPunctuation:
        goto access_punctuation_no_emit;
    case State::Error:
        VERIFY_NOT_REACHED();
    }
    // SwitchState expression
expression_with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
expression_no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::Expression;
expression_as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        goto expression_no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
            goto expression_no_emit;
        }
        tokEnd += 1;
        goto expression_no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::ExclaimEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::LogicalNotExpr
        carriedEmitNodeKind = NodeKind::LogicalNotExpr;
        // next expression
        goto expression_with_emit;
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
        // emitNode NodeKind::ParenthesizedExpr
        carriedEmitNodeKind = NodeKind::ParenthesizedExpr;
        // next first_argument_paren
        goto first_argument_paren_with_emit;
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
        // emitNode NodeKind::DereferenceExpr
        carriedEmitNodeKind = NodeKind::DereferenceExpr;
        // next expression
        goto expression_with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitNode NodeKind::PreIncrementExpr
            carriedEmitNodeKind = NodeKind::PreIncrementExpr;
            // next expression
            goto expression_with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::PlusEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::PlusExpr
        carriedEmitNodeKind = NodeKind::PlusExpr;
        // next expression
        goto expression_with_emit;
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
            // emitNode NodeKind::PreDecrementExpr
            carriedEmitNodeKind = NodeKind::PreDecrementExpr;
            // next expression
            goto expression_with_emit;
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
        // emitNode NodeKind::NegateExpr
        carriedEmitNodeKind = NodeKind::NegateExpr;
        // next expression
        goto expression_with_emit;
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
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);
            goto expression_no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);
            goto expression_no_emit;
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
        // emitNode NodeKind::BitwiseNotExpr
        carriedEmitNodeKind = NodeKind::BitwiseNotExpr;
        // next expression
        goto expression_with_emit;
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
        goto expression_word_case;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
expression_word_case:
    {
        auto wordAndPos = readWord(tokEnd, state.wordTable);
        tokEnd = wordAndPos.position;
        word = wordAndPos.word;
    }
    if (word.keyword()) {
    [[maybe_unused]] expression_keyword_check:
        if (word == words["if"]) {
            // pushScope ScopeKind::IfExpr
            scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
            // next expression
            goto expression_no_emit;
        }
        goto error_keyword_check;
    }
    // emitNode NodeKind::IdentifierExpr
    carriedEmitNodeKind = NodeKind::IdentifierExpr;
    // next after_expression
    goto after_expression_with_emit;

    // SwitchState after_expression
after_expression_with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
after_expression_no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::AfterExpression;
after_expression_as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        goto after_expression_no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
            goto after_expression_no_emit;
        }
        tokEnd += 1;
        goto after_expression_no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitNode NodeKind::CompareNotEqualExpr
            carriedEmitNodeKind = NodeKind::CompareNotEqualExpr;
            // next expression
            goto expression_with_emit;
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
        // emitNode NodeKind::RemainderExpr
        carriedEmitNodeKind = NodeKind::RemainderExpr;
        // next expression
        goto expression_with_emit;
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
            // emitNode NodeKind::LogicalAndExpr
            carriedEmitNodeKind = NodeKind::LogicalAndExpr;
            // next expression
            goto expression_with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::AmpEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::BitwiseAndExpr
        carriedEmitNodeKind = NodeKind::BitwiseAndExpr;
        // next expression
        goto expression_with_emit;
    }
    case '(': {
        tokEnd += 1;
        // emitNode NodeKind::CallExpr
        carriedEmitNodeKind = NodeKind::CallExpr;
        // next first_argument_paren
        goto first_argument_paren_with_emit;
    }
    case ')': {
        tokEnd += 1;
        // popScope ScopeKind::Paren
        scopePosition = popScope(scopePosition, ScopeKind::Paren);
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression_with_emit;
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
        // emitNode NodeKind::MultiplyExpr
        carriedEmitNodeKind = NodeKind::MultiplyExpr;
        // next expression
        goto expression_with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitNode NodeKind::PostIncrementExpr
            carriedEmitNodeKind = NodeKind::PostIncrementExpr;
            // next after_expression
            goto after_expression_with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::PlusEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::AdditionExpr
        carriedEmitNodeKind = NodeKind::AdditionExpr;
        // next expression
        goto expression_with_emit;
    }
    case ',': {
        tokEnd += 1;
        // next comma_after_expression
        // inlined comma_after_expression
        tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
        tokBegin = tokEnd;
        parseState = State::CommaAfterExpression;
        if (std::string_view(tokEnd, 1) == ")"sv) {
            tokEnd += 1;
            // popScope ScopeKind::Paren
            scopePosition = popScope(scopePosition, ScopeKind::Paren);
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        if (std::string_view(tokEnd, 1) == "]"sv) {
            tokEnd += 1;
            // popScope ScopeKind::Square
            scopePosition = popScope(scopePosition, ScopeKind::Square);
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        if (std::string_view(tokEnd, 1) == "}"sv) {
            tokEnd += 1;
            // popScope ScopeKind::Brace
            scopePosition = popScope(scopePosition, ScopeKind::Brace);
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        if (isWordFirstCharacter(tokEnd[0])) {
            {
                auto wordAndPos = readWord(tokEnd, state.wordTable);
                tokEnd = wordAndPos.position;
                word = wordAndPos.word;
            }
            if (word.keyword()) {
            [[maybe_unused]] comma_after_expression_keyword_check:
                if (word == words["else"]) {
                    // next comma_else
                    // inlined comma_else
                    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
                    tokBegin = tokEnd;
                    parseState = State::CommaElse;
                    if (std::string_view(tokEnd, 2) == "=>"sv) {
                        tokEnd += 2;
                        // emitNode NodeKind::CommaElseExpr
                        carriedEmitNodeKind = NodeKind::CommaElseExpr;
                        // next expression
                        goto expression_with_emit;
                    }
                    // then error
                    goto error_as_then;
                }
                goto check_designated_argument_keyword_check;
            }
            // savedToken = tok
            savedTokenBegin = tokBegin;
            savedTokenEnd = tokEnd;
            // next maybe_designated_argument
            // inlined maybe_designated_argument
            tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
            tokBegin = tokEnd;
            parseState = State::MaybeDesignatedArgument;
            if (std::string_view(tokEnd, 1) == "="sv) {
                char next = tokEnd[1];
                if (next != '=' && next != '>') {
                    tokEnd += 1;
                    // emitNode NodeKind::DesignateArgument, savedToken
                    emitNode(NodeKind::DesignateArgument, savedTokenBegin, savedTokenEnd, state, sourceBufferBegin);
                    // next expression
                    goto expression_no_emit;
                }
            }
            // emitNode NodeKind::IdentifierExpr, savedToken
            emitNode(NodeKind::IdentifierExpr, savedTokenBegin, savedTokenEnd, state, sourceBufferBegin);
            // then after_expression
            goto after_expression_as_then;
        }
        // then check_designated_argument
        goto check_designated_argument_as_then;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // emitNode NodeKind::PostDecrementExpr
            carriedEmitNodeKind = NodeKind::PostDecrementExpr;
            // next after_expression
            goto after_expression_with_emit;
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
        // emitNode NodeKind::SubtractionExpr
        carriedEmitNodeKind = NodeKind::SubtractionExpr;
        // next expression
        goto expression_with_emit;
    }
    case '.': {
        tokEnd += 1;
        // nodeKind = NodeKind::MemberAccessExpr
        nodeKind = NodeKind::MemberAccessExpr;
        // next access_punctuation
        goto access_punctuation_no_emit;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);
            goto after_expression_no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);
            goto after_expression_no_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::SlashEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::DivideExpr
        carriedEmitNodeKind = NodeKind::DivideExpr;
        // next expression
        goto expression_with_emit;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // nodeKind = NodeKind::StaticAccessExpr
            nodeKind = NodeKind::StaticAccessExpr;
            // next access_punctuation
            goto access_punctuation_no_emit;
        }
        tokEnd += 1;
        // popScope ScopeKind::IfExprOrStmt
        scopePosition = popScope(scopePosition, ScopeKind::IfExprOrStmt);
        // emitNode NodeKind::IfStmt
        carriedEmitNodeKind = NodeKind::IfStmt;
        // next single_or_compound_statement
        // inlined single_or_compound_statement
        emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
        tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
        tokBegin = tokEnd;
        parseState = State::SingleOrCompoundStatement;
        if (std::string_view(tokEnd, 1) == "{"sv) {
            tokEnd += 1;
            // pushScope ScopeKind::CompoundStmt
            scopePosition = pushScope(scopePosition, ScopeKind::CompoundStmt);
            // emitNode NodeKind::CompoundStmt
            carriedEmitNodeKind = NodeKind::CompoundStmt;
            // next statement
            goto statement_with_emit;
        }
        // then statement
        goto statement_as_then;
    }
    case ';': {
        tokEnd += 1;
        // exitIfUnscoped
        if (scopePosition[0] == ScopeKind::Invalid) {
            return state.nodes;
        }
        // emitNode NodeKind::ExpressionStmt
        carriedEmitNodeKind = NodeKind::ExpressionStmt;
        // next statement
        goto statement_with_emit;
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
            // emitNode NodeKind::ShiftLeftExpr
            carriedEmitNodeKind = NodeKind::ShiftLeftExpr;
            // next expression
            goto expression_with_emit;
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
            // emitNode NodeKind::CompareLessEqualExpr
            carriedEmitNodeKind = NodeKind::CompareLessEqualExpr;
            // next expression
            goto expression_with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::CompareLessExpr
        carriedEmitNodeKind = NodeKind::CompareLessExpr;
        // next expression
        goto expression_with_emit;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitNode NodeKind::CompareEqualExpr
            carriedEmitNodeKind = NodeKind::CompareEqualExpr;
            // next expression
            goto expression_with_emit;
        }
        if (next == '>') {
            tokEnd += 2;
            // popScope ScopeKind::IfExpr, ScopeKind::IfExprOrStmt
            scopePosition = popScope(scopePosition, ScopeKind::IfExpr, ScopeKind::IfExprOrStmt);
            // emitNode NodeKind::IfExpr
            carriedEmitNodeKind = NodeKind::IfExpr;
            // next expression
            goto expression_with_emit;
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
            // emitNode NodeKind::CompareGreaterEqualExpr
            carriedEmitNodeKind = NodeKind::CompareGreaterEqualExpr;
            // next expression
            goto expression_with_emit;
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
            // emitNode NodeKind::ShiftRightExpr
            carriedEmitNodeKind = NodeKind::ShiftRightExpr;
            // next expression
            goto expression_with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::CompareGreaterExpr
        carriedEmitNodeKind = NodeKind::CompareGreaterExpr;
        // next expression
        goto expression_with_emit;
    }
    case '[': {
        tokEnd += 1;
        // emitNode NodeKind::IndexExpr
        carriedEmitNodeKind = NodeKind::IndexExpr;
        // next first_argument_square
        // inlined first_argument_square
        emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
        tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
        tokBegin = tokEnd;
        parseState = State::FirstArgumentSquare;
        if (std::string_view(tokEnd, 1) == "]"sv) {
            tokEnd += 1;
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        // pushScope ScopeKind::Square
        scopePosition = pushScope(scopePosition, ScopeKind::Square);
        // then check_designated_argument
        goto check_designated_argument_as_then;
    }
    case ']': {
        tokEnd += 1;
        // popScope ScopeKind::Square
        scopePosition = popScope(scopePosition, ScopeKind::Square);
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression_with_emit;
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
        // emitNode NodeKind::BitwiseXorExpr
        carriedEmitNodeKind = NodeKind::BitwiseXorExpr;
        // next expression
        goto expression_with_emit;
    }
    case '{': {
        tokEnd += 1;
        // emitNode NodeKind::Parameterize
        carriedEmitNodeKind = NodeKind::Parameterize;
        // next first_argument_brace
        // inlined first_argument_brace
        emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
        tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
        tokBegin = tokEnd;
        parseState = State::FirstArgumentBrace;
        if (std::string_view(tokEnd, 1) == "}"sv) {
            tokEnd += 1;
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        // pushScope ScopeKind::Brace
        scopePosition = pushScope(scopePosition, ScopeKind::Brace);
        // then check_designated_argument
        goto check_designated_argument_as_then;
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
            // emitNode NodeKind::LogicalOrExpr
            carriedEmitNodeKind = NodeKind::LogicalOrExpr;
            // next expression
            goto expression_with_emit;
        }
        tokEnd += 1;
        // emitNode NodeKind::BitwiseOrExpr
        carriedEmitNodeKind = NodeKind::BitwiseOrExpr;
        // next expression
        goto expression_with_emit;
    }
    case '}': {
        tokEnd += 1;
        // popScope ScopeKind::Brace
        scopePosition = popScope(scopePosition, ScopeKind::Brace);
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression_with_emit;
    }
    case '~': {
        tokEnd += 1;
        // error
        errorToken = Token::Tilde;
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
        goto after_expression_word_case;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
after_expression_word_case:
    {
        auto wordAndPos = readWord(tokEnd, state.wordTable);
        tokEnd = wordAndPos.position;
        word = wordAndPos.word;
    }
    if (word.keyword()) {
    [[maybe_unused]] after_expression_keyword_check:
        goto error_keyword_check;
    }
    // error
    errorToken = Token::Identifier;
    goto handle_parse_error;

    // SwitchState statement
statement_with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
statement_no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::Statement;
statement_as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        goto statement_no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
            goto statement_no_emit;
        }
        tokEnd += 1;
        goto statement_no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::ExclaimEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::LogicalNotExpr
        carriedEmitNodeKind = NodeKind::LogicalNotExpr;
        // next expression
        goto expression_with_emit;
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
        // emitNode NodeKind::ParenthesizedExpr
        carriedEmitNodeKind = NodeKind::ParenthesizedExpr;
        // next first_argument_paren
        goto first_argument_paren_with_emit;
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
        // emitNode NodeKind::DereferenceExpr
        carriedEmitNodeKind = NodeKind::DereferenceExpr;
        // next expression
        goto expression_with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitNode NodeKind::PreIncrementExpr
            carriedEmitNodeKind = NodeKind::PreIncrementExpr;
            // next expression
            goto expression_with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            errorToken = Token::PlusEqual;
            goto handle_parse_error;
        }
        tokEnd += 1;
        // emitNode NodeKind::PlusExpr
        carriedEmitNodeKind = NodeKind::PlusExpr;
        // next expression
        goto expression_with_emit;
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
            // emitNode NodeKind::PreDecrementExpr
            carriedEmitNodeKind = NodeKind::PreDecrementExpr;
            // next expression
            goto expression_with_emit;
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
        // emitNode NodeKind::NegateExpr
        carriedEmitNodeKind = NodeKind::NegateExpr;
        // next expression
        goto expression_with_emit;
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
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);
            goto statement_no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);
            goto statement_no_emit;
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
        // popScope ScopeKind::CompoundStmt
        scopePosition = popScope(scopePosition, ScopeKind::CompoundStmt);
        // exitIfUnscoped
        if (scopePosition[0] == ScopeKind::Invalid) {
            return state.nodes;
        }
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next statement
        goto statement_with_emit;
    }
    case '~': {
        tokEnd += 1;
        // emitNode NodeKind::BitwiseNotExpr
        carriedEmitNodeKind = NodeKind::BitwiseNotExpr;
        // next expression
        goto expression_with_emit;
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
        goto statement_word_case;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
statement_word_case:
    {
        auto wordAndPos = readWord(tokEnd, state.wordTable);
        tokEnd = wordAndPos.position;
        word = wordAndPos.word;
    }
    if (word.keyword()) {
    [[maybe_unused]] statement_keyword_check:
        if (word == words["if"]) {
            // pushScope ScopeKind::IfExprOrStmt
            scopePosition = pushScope(scopePosition, ScopeKind::IfExprOrStmt);
            // next expression
            goto expression_no_emit;
        }
        goto expression_keyword_check;
    }
    // emitNode NodeKind::IdentifierExpr
    carriedEmitNodeKind = NodeKind::IdentifierExpr;
    // next after_expression
    goto after_expression_with_emit;

    // LinearState check_designated_argument
check_designated_argument_as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] check_designated_argument_keyword_check:
            goto expression_keyword_check;
        }
        // savedToken = tok
        savedTokenBegin = tokBegin;
        savedTokenEnd = tokEnd;
        // next maybe_designated_argument
        // inlined maybe_designated_argument
        tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
        tokBegin = tokEnd;
        parseState = State::MaybeDesignatedArgument;
        if (std::string_view(tokEnd, 1) == "="sv) {
            char next = tokEnd[1];
            if (next != '=' && next != '>') {
                tokEnd += 1;
                // emitNode NodeKind::DesignateArgument, savedToken
                emitNode(NodeKind::DesignateArgument, savedTokenBegin, savedTokenEnd, state, sourceBufferBegin);
                // next expression
                goto expression_no_emit;
            }
        }
        // emitNode NodeKind::IdentifierExpr, savedToken
        emitNode(NodeKind::IdentifierExpr, savedTokenBegin, savedTokenEnd, state, sourceBufferBegin);
        // then after_expression
        goto after_expression_as_then;
    }
    // then expression
    goto expression_as_then;

    // LinearState first_argument_paren
first_argument_paren_with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
first_argument_paren_no_emit:
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentParen;
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression_with_emit;
    }
    // pushScope ScopeKind::Paren
    scopePosition = pushScope(scopePosition, ScopeKind::Paren);
    // then check_designated_argument
    goto check_designated_argument_as_then;

    // LinearState access_punctuation
access_punctuation_no_emit:
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    tokBegin = tokEnd;
    parseState = State::AccessPunctuation;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, state.wordTable);
            tokEnd = wordAndPos.position;
            word = wordAndPos.word;
        }
        if (word.keyword()) {
        [[maybe_unused]] access_punctuation_keyword_check:
            goto error_keyword_check;
        }
        // emitNode nodeKind
        carriedEmitNodeKind = nodeKind;
        // next after_expression
        goto after_expression_with_emit;
    }
    // then error
    goto error_as_then;

    // SwitchState error
error_as_then:
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
        goto error_word_case;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
error_word_case:
error_keyword_check:
    VERIFY_NOT_REACHED();


handle_parse_error:
    errorHandler->invalidToken(parseState, errorToken);
    return {};
}

std::string_view nameString(NodeKind kind) {
    switch (kind) {
#define NODE(kind, type, prec) \
    case NodeKind::kind:       \
        return #kind;

#include "nodes.inc"
    }
}

}