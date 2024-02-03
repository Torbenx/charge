#include "nodes.h"
#include <utility>

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
    Paren = ')',
    Square = ']',
    Brace = '}',
};
static char scopeKindToRightBracket(ScopeKind scope) {
    return std::to_underlying(scope);
}
static bool isBracketScope(ScopeKind scope) {
    return std::to_underlying(scope) >= ' ';
}

struct ParseStackState {
    std::vector<Node> nodes;
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
    position[0] = kind;
    position += 1;
    return position;
}

template<typename... Args>
static ScopeKind* popScope(ScopeKind* position, Args... kinds) {
    static_assert((std::is_same_v<Args, ScopeKind> && ...));
    auto index = ScopeBuffer::toIndex(position);
    VERIFY(index != 0);
    position -= 1;
    VERIFY(((position[0] == kinds) || ...));
    return position;
}

static ScopeKind peekScope(ScopeKind* position) {
    return position[-1];
}

static void emitNode(NodeKind kind, const char* begin, const char* end, ParseStackState& state, const char* sourceBufferBegin) {
    state.nodes.push_back({ kind, (uint32_t)(begin - sourceBufferBegin), (uint32_t)(end - sourceBufferBegin) });
}

[[nodiscard]] static const char* skipToEndOfIdentifier(const char* position) {
    do {
        position += 1;
    } while (isWordBulkCharacter(position[0]));
    return position;
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

[[nodiscard]] static const char* inlineAdvancer(const char* tokEnd, ParseStackState& state, const char* sourceBufferBegin) {
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

static std::vector<Node> reachedEOS(ParseStackState& state, ScopeKind* scopePosition) {
    scopePosition -= 1;
    VERIFY(scopePosition[0] == ScopeKind::Invalid);
    VERIFY(((uintptr_t)scopePosition & (uintptr_t)(SCOPE_BUFFER_SIZE - 1)) == 0);
    return state.nodes;
}

std::vector<Node> parse(std::string_view sourceBuf) {
    ScopeBuffer scopeBuffer;
    ScopeKind* scopePosition = scopeBuffer.buffer;
    scopePosition[0] = ScopeKind::Invalid;
    scopePosition += 1;

    ParseStackState state;
    const char* sourceBufferBegin = sourceBuf.begin();
    const char* tokBegin = sourceBufferBegin;
    const char* tokEnd = sourceBufferBegin;
    NodeKind carriedEmitNodeKind = (NodeKind)0;

    const char* savedTokenBegin = nullptr;
    const char* savedTokenEnd = nullptr;
    NodeKind nodeKind = (NodeKind)0;

    goto expression_no_emit;

#define TODO_PARSE() VERIFY_NOT_REACHED()
#define TODO_ERROR(error) VERIFY_NOT_REACHED()

    // SwitchState expression
expression_with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
expression_no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
expression_no_whitespace:
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
            VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
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
        VERIFY_NOT_REACHED();
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
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
        VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
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
        VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case ';': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '?': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '[': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case ']': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '{': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '}': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '~': {
        tokEnd += 1;
        // emitNode NodeKind::BitwiseNotExpr
        carriedEmitNodeKind = NodeKind::BitwiseNotExpr;
        // next expression
        goto expression_with_emit;
    }
    case 'a': {
        if (std::string_view(tokEnd + 1, 7) == "nalysis" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "ssert" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "ssign" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'b': {
        if (std::string_view(tokEnd + 1, 4) == "reak" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'c': {
        if (std::string_view(tokEnd + 1, 7) == "ontinue" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'd': {
        if (std::string_view(tokEnd + 1, 1) == "o" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'e': {
        if (std::string_view(tokEnd + 1, 3) == "lif" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "lse" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'f': {
        if (std::string_view(tokEnd + 1, 2) == "or" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 6) == "orward" && !isWordBulkCharacter(tokEnd[7])) {
            tokEnd += 7;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "alse" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'g': {
        if (std::string_view(tokEnd + 1, 4) == "uard" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'i': {
        if (std::string_view(tokEnd + 1, 1) == "f" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // pushScope ScopeKind::IfExpr
            scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
            // next expression
            goto expression_no_emit;
        }
        if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "nout" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'l': {
        if (std::string_view(tokEnd + 1, 3) == "oop" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 2) == "et" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'm': {
        if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'n': {
        if (std::string_view(tokEnd + 1, 8) == "amespace" && !isWordBulkCharacter(tokEnd[9])) {
            tokEnd += 9;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'o': {
        if (std::string_view(tokEnd + 1, 5) == "bject" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 2) == "ut" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'r': {
        if (std::string_view(tokEnd + 1, 5) == "eturn" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 's': {
        if (std::string_view(tokEnd + 1, 5) == "truct" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "tatic" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 't': {
        if (std::string_view(tokEnd + 1, 2) == "ry" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "rait" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 7) == "emplate" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "rue" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'v': {
        if (std::string_view(tokEnd + 1, 2) == "ar" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
    case 'w': {
        if (std::string_view(tokEnd + 1, 4) == "hile" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "ith" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto expression_identifier_case;
    }
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
    case 'h':
    case 'j':
    case 'k':
    case 'p':
    case 'q':
    case 'u':
    case 'x':
    case 'y':
    case 'z':
    case '#':
    case '$':
    case '_': {
        goto expression_identifier_case;
    }
    default: {
        return state.nodes;
    }
    } // switch
    VERIFY_NOT_REACHED();
expression_identifier_case:
    tokEnd = skipToEndOfIdentifier(tokEnd);
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
after_expression_no_whitespace:
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
        VERIFY_NOT_REACHED();
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
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
                VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
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
        if (std::string_view(tokEnd, 4) == "else" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // next comma_else
            // inlined comma_else
            tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
            tokBegin = tokEnd;
            if (std::string_view(tokEnd, 2) == "=>") {
                tokEnd += 2;
                // emitNode NodeKind::CommaElseExpr
                carriedEmitNodeKind = NodeKind::CommaElseExpr;
                // next expression
                goto expression_with_emit;
            }
            // then error
            goto error_no_whitespace;
        }
        if (std::string_view(tokEnd, 1) == ")") {
            tokEnd += 1;
            // popScope ScopeKind::Paren
            scopePosition = popScope(scopePosition, ScopeKind::Paren);
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        if (std::string_view(tokEnd, 1) == "]") {
            tokEnd += 1;
            // popScope ScopeKind::Square
            scopePosition = popScope(scopePosition, ScopeKind::Square);
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        if (std::string_view(tokEnd, 1) == "}") {
            tokEnd += 1;
            // popScope ScopeKind::Brace
            scopePosition = popScope(scopePosition, ScopeKind::Brace);
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        // then check_designated_argument
        goto check_designated_argument_no_whitespace;
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
            VERIFY_NOT_REACHED();
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
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
        if (std::string_view(tokEnd, 1) == "{") {
            tokEnd += 1;
            // pushScope ScopeKind::CompoundStmt
            scopePosition = pushScope(scopePosition, ScopeKind::CompoundStmt);
            // emitNode NodeKind::CompoundStmt
            carriedEmitNodeKind = NodeKind::CompoundStmt;
            // next statement
            goto statement_with_emit;
        }
        // then statement
        goto statement_no_whitespace;
    }
    case ';': {
        tokEnd += 1;
        // exitIfUnscoped
        if (ScopeBuffer::toIndex(scopePosition) == 0) {
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
                VERIFY_NOT_REACHED();
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
                VERIFY_NOT_REACHED();
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
        VERIFY_NOT_REACHED();
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
                VERIFY_NOT_REACHED();
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
    case '?': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
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
        if (std::string_view(tokEnd, 1) == "]") {
            tokEnd += 1;
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        // pushScope ScopeKind::Square
        scopePosition = pushScope(scopePosition, ScopeKind::Square);
        // then check_designated_argument
        goto check_designated_argument_no_whitespace;
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
            VERIFY_NOT_REACHED();
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
        if (std::string_view(tokEnd, 1) == "}") {
            tokEnd += 1;
            // emitNode NodeKind::EmptyNode
            carriedEmitNodeKind = NodeKind::EmptyNode;
            // next after_expression
            goto after_expression_with_emit;
        }
        // pushScope ScopeKind::Brace
        scopePosition = pushScope(scopePosition, ScopeKind::Brace);
        // then check_designated_argument
        goto check_designated_argument_no_whitespace;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
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
        VERIFY_NOT_REACHED();
    }
    case 'a': {
        if (std::string_view(tokEnd + 1, 7) == "nalysis" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "ssert" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "ssign" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'b': {
        if (std::string_view(tokEnd + 1, 4) == "reak" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'c': {
        if (std::string_view(tokEnd + 1, 7) == "ontinue" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'd': {
        if (std::string_view(tokEnd + 1, 1) == "o" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'e': {
        if (std::string_view(tokEnd + 1, 3) == "lif" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "lse" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'f': {
        if (std::string_view(tokEnd + 1, 2) == "or" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 6) == "orward" && !isWordBulkCharacter(tokEnd[7])) {
            tokEnd += 7;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "alse" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'g': {
        if (std::string_view(tokEnd + 1, 4) == "uard" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'i': {
        if (std::string_view(tokEnd + 1, 1) == "f" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "nout" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'l': {
        if (std::string_view(tokEnd + 1, 3) == "oop" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 2) == "et" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'm': {
        if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'n': {
        if (std::string_view(tokEnd + 1, 8) == "amespace" && !isWordBulkCharacter(tokEnd[9])) {
            tokEnd += 9;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'o': {
        if (std::string_view(tokEnd + 1, 5) == "bject" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 2) == "ut" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'r': {
        if (std::string_view(tokEnd + 1, 5) == "eturn" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 's': {
        if (std::string_view(tokEnd + 1, 5) == "truct" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "tatic" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 't': {
        if (std::string_view(tokEnd + 1, 2) == "ry" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "rait" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 7) == "emplate" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "rue" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'v': {
        if (std::string_view(tokEnd + 1, 2) == "ar" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
    case 'w': {
        if (std::string_view(tokEnd + 1, 4) == "hile" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "ith" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto after_expression_identifier_case;
    }
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
    case 'h':
    case 'j':
    case 'k':
    case 'p':
    case 'q':
    case 'u':
    case 'x':
    case 'y':
    case 'z':
    case '#':
    case '$':
    case '_': {
        goto after_expression_identifier_case;
    }
    default: {
        return state.nodes;
    }
    } // switch
    VERIFY_NOT_REACHED();
after_expression_identifier_case:
    tokEnd = skipToEndOfIdentifier(tokEnd);
    // error
    VERIFY_NOT_REACHED();

    // SwitchState statement
statement_with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
statement_no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
statement_no_whitespace:
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
            VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
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
        VERIFY_NOT_REACHED();
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
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
        VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
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
        VERIFY_NOT_REACHED();
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
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case ';': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '>') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '?': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '[': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case ']': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '{': {
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // error
                VERIFY_NOT_REACHED();
            }
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        tokEnd += 1;
        // error
        VERIFY_NOT_REACHED();
    }
    case '}': {
        tokEnd += 1;
        // popScope ScopeKind::CompoundStmt
        scopePosition = popScope(scopePosition, ScopeKind::CompoundStmt);
        // exitIfUnscoped
        if (ScopeBuffer::toIndex(scopePosition) == 0) {
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
    case 'a': {
        if (std::string_view(tokEnd + 1, 7) == "nalysis" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "ssert" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "ssign" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'b': {
        if (std::string_view(tokEnd + 1, 4) == "reak" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'c': {
        if (std::string_view(tokEnd + 1, 7) == "ontinue" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'd': {
        if (std::string_view(tokEnd + 1, 1) == "o" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'e': {
        if (std::string_view(tokEnd + 1, 3) == "lif" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "lse" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'f': {
        if (std::string_view(tokEnd + 1, 2) == "or" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 6) == "orward" && !isWordBulkCharacter(tokEnd[7])) {
            tokEnd += 7;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "alse" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'g': {
        if (std::string_view(tokEnd + 1, 4) == "uard" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'i': {
        if (std::string_view(tokEnd + 1, 1) == "f" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // pushScope ScopeKind::IfExprOrStmt
            scopePosition = pushScope(scopePosition, ScopeKind::IfExprOrStmt);
            // next expression
            goto expression_no_emit;
        }
        if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
            tokEnd += 2;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "nout" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'l': {
        if (std::string_view(tokEnd + 1, 3) == "oop" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 2) == "et" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'm': {
        if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'n': {
        if (std::string_view(tokEnd + 1, 8) == "amespace" && !isWordBulkCharacter(tokEnd[9])) {
            tokEnd += 9;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'o': {
        if (std::string_view(tokEnd + 1, 5) == "bject" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 2) == "ut" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'r': {
        if (std::string_view(tokEnd + 1, 5) == "eturn" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 's': {
        if (std::string_view(tokEnd + 1, 5) == "truct" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 5) == "tatic" && !isWordBulkCharacter(tokEnd[6])) {
            tokEnd += 6;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 't': {
        if (std::string_view(tokEnd + 1, 2) == "ry" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 4) == "rait" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 7) == "emplate" && !isWordBulkCharacter(tokEnd[8])) {
            tokEnd += 8;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "rue" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'v': {
        if (std::string_view(tokEnd + 1, 2) == "ar" && !isWordBulkCharacter(tokEnd[3])) {
            tokEnd += 3;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
    case 'w': {
        if (std::string_view(tokEnd + 1, 4) == "hile" && !isWordBulkCharacter(tokEnd[5])) {
            tokEnd += 5;
            // error
            VERIFY_NOT_REACHED();
        }
        if (std::string_view(tokEnd + 1, 3) == "ith" && !isWordBulkCharacter(tokEnd[4])) {
            tokEnd += 4;
            // error
            VERIFY_NOT_REACHED();
        }
        goto statement_identifier_case;
    }
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
    case 'h':
    case 'j':
    case 'k':
    case 'p':
    case 'q':
    case 'u':
    case 'x':
    case 'y':
    case 'z':
    case '#':
    case '$':
    case '_': {
        goto statement_identifier_case;
    }
    default: {
        return state.nodes;
    }
    } // switch
    VERIFY_NOT_REACHED();
statement_identifier_case:
    tokEnd = skipToEndOfIdentifier(tokEnd);
    // emitNode NodeKind::IdentifierExpr
    carriedEmitNodeKind = NodeKind::IdentifierExpr;
    // next after_expression
    goto after_expression_with_emit;

    // LinearState check_designated_argument
check_designated_argument_no_whitespace:
    if (isWordFirstCharacter(tokEnd[0])) {
        tokEnd = skipToEndOfIdentifier(tokEnd);
        // savedToken = tok
        savedTokenBegin = tokBegin;
        savedTokenEnd = tokEnd;
        // next maybe_designated_argument
        // inlined maybe_designated_argument
        tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
        tokBegin = tokEnd;
        if (std::string_view(tokEnd, 1) == "=") {
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
        goto after_expression_no_whitespace;
    }
    // then expression
    goto expression_no_whitespace;

    // LinearState first_argument_paren
first_argument_paren_with_emit:
    emitNode(carriedEmitNodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    tokBegin = tokEnd;
    if (std::string_view(tokEnd, 1) == ")") {
        tokEnd += 1;
        // emitNode NodeKind::EmptyNode
        carriedEmitNodeKind = NodeKind::EmptyNode;
        // next after_expression
        goto after_expression_with_emit;
    }
    // pushScope ScopeKind::Paren
    scopePosition = pushScope(scopePosition, ScopeKind::Paren);
    // then check_designated_argument
    goto check_designated_argument_no_whitespace;

    // LinearState access_punctuation
access_punctuation_no_emit:
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    tokBegin = tokEnd;
    if (isWordFirstCharacter(tokEnd[0])) {
        tokEnd = skipToEndOfIdentifier(tokEnd);
        // emitNode nodeKind
        carriedEmitNodeKind = nodeKind;
        // next after_expression
        goto after_expression_with_emit;
    }
    // then error
    goto error_no_whitespace;


error_no_whitespace:
    VERIFY_NOT_REACHED();
}

std::string_view nameString(NodeKind kind) {
    switch (kind) {
#define NODE(kind, type, prec) \
    case NodeKind::kind:       \
        return #kind;

#include "nodes.inc"
    }
}