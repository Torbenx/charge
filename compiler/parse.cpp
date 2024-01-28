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
    const char* sourceBufferEnd;

    ParseStackState(std::string_view sourceBuf)
        : sourceBufferEnd(sourceBuf.end()) { }
};

static constexpr int_t SCOPE_BUFFER_SIZE = 1024;

static ScopeKind* pushScope(ScopeKind kind, ScopeKind* position) {
    uintptr_t scopeBufferEnd = ((uintptr_t)position & ~(uintptr_t)(SCOPE_BUFFER_SIZE - 1)) + SCOPE_BUFFER_SIZE;
    VERIFY((uintptr_t)position < scopeBufferEnd);
    position[0] = kind;
    position += 1;
    return position;
}

static ScopeKind* popScope(ScopeKind kind, ScopeKind* position) {
    VERIFY(kind != ScopeKind::Invalid);
    position -= 1;
    VERIFY(position[0] == kind);
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

struct ScopeBuffer {
    ScopeKind* buffer;
    ScopeBuffer()
        : buffer((ScopeKind*)::operator new(SCOPE_BUFFER_SIZE, std::align_val_t(SCOPE_BUFFER_SIZE))) { }
    ~ScopeBuffer() {
        ::operator delete(buffer, SCOPE_BUFFER_SIZE, std::align_val_t(SCOPE_BUFFER_SIZE));
    }
};

static std::vector<Node> reachedEOS(ParseStackState& state, ScopeKind* scopePosition) {
    scopePosition -= 1;
    VERIFY(scopePosition[0] == ScopeKind::Invalid);
    VERIFY(((uintptr_t)scopePosition & (uintptr_t)(SCOPE_BUFFER_SIZE - 1)) == 0);
    return state.nodes;
}

enum class LexerError {
    InvalidCharacter,
};

std::vector<Node> parse(std::string_view sourceBuf) {
    ScopeBuffer scopeBuffer;
    ScopeKind* scopePosition = scopeBuffer.buffer;
    scopePosition[0] = ScopeKind::Invalid;
    scopePosition += 1;

    ParseStackState state(sourceBuf);
    const char* sourceBufferBegin = sourceBuf.begin();
    const char* tokBegin = sourceBufferBegin;
    const char* tokEnd = sourceBufferBegin;
    NodeKind nodeKind = (NodeKind)0;
    size_t data1 = 0;

    nodeKind = NodeKind::Newline;
    goto expression;

#define TODO_PARSE() VERIFY_NOT_REACHED()
#define TODO_ERROR(error) VERIFY_NOT_REACHED()

check_for_designated_argument : {
    tokEnd = skipToEndOfIdentifier(tokEnd);
    auto savedBegin = tokBegin;
    auto savedEnd = tokEnd;
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    tokBegin = tokEnd;
    if (std::string_view(tokEnd, 1) == "=") {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            nodeKind = NodeKind::DesignateArgument;
            goto expression;
        }
    }
    emitNode(NodeKind::IdentifierExpr, savedBegin, savedEnd, state, sourceBufferBegin);
    goto after_expression_dispatch;
}
begin_argument_scope : {
    emitNode(nodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
    ScopeKind scopeKind = (ScopeKind)data1;
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    tokBegin = tokEnd;
    if (tokEnd[0] == scopeKindToRightBracket(scopeKind)) {
        tokEnd += 1;
        nodeKind = NodeKind::EmptyNode;
        goto after_expression;
    }
    scopePosition = pushScope(scopeKind, scopePosition);
    if (isWordFirstCharacter(tokEnd[0])) {
        goto check_for_designated_argument;
    }
    goto expression_dispatch;
}
single_or_compound_statement : {
    emitNode(nodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    tokBegin = tokEnd;
    if (std::string_view(tokEnd, 1) == "{") {
        tokEnd += 1;
        scopePosition = pushScope(ScopeKind::CompoundStmt, scopePosition);
        nodeKind = NodeKind::CompoundStmt;
        goto statement;
    }
    goto statement_dispatch;
}

statement:
    emitNode(nodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
statement_continue:
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        tokBegin = tokEnd;
        nodeKind = (NodeKind)0;
        data1 = 0;
    statement_dispatch:
        fmt::println("statement: {}", tokEnd[0]);
        switch (tokEnd[0]) {
        case '\n': {
            tokEnd += 1;
            continue;
        }
        case '\r': {
            if (tokEnd[1] == '\n') {
                tokEnd += 2;
                continue;
            }
            tokEnd += 1;
            continue;
        }
        case '!': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::LogicalNotExpr;
            goto expression;
        }
        case '%': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '&': {
            char next = tokEnd[1];
            if (next == '&') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '(': {
            tokEnd += 1;
            nodeKind = NodeKind::ParenthesizedExpr;
            data1 = (size_t)ScopeKind::Paren;
            goto begin_argument_scope;
        }
        case ')': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '*': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::DereferenceExpr;
            goto expression;
        }
        case '+': {
            char next = tokEnd[1];
            if (next == '+') {
                tokEnd += 2;
                nodeKind = NodeKind::PreIncrementExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::PlusExpr;
            goto expression;
        }
        case ',': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '-': {
            char next = tokEnd[1];
            if (next == '-') {
                tokEnd += 2;
                nodeKind = NodeKind::PreDecrementExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '>') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::NegateExpr;
            goto expression;
        }
        case '.': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '/': {
            char next = tokEnd[1];
            if (next == '*') {
                tokEnd += 2;
                tokEnd = skipToEndOfBlockComment(tokEnd);
                tokEnd += 2;
                emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);
                continue;
            }
            if (next == '/') {
                tokEnd += 2;
                tokEnd = skipToEndOfLine(tokEnd);
                emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);
                continue;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case ':': {
            char next = tokEnd[1];
            if (next == ':') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case ';': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '<': {
            char next = tokEnd[1];
            if (next == '<') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '=') {
                char next = tokEnd[2];
                if (next == '>') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '=': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '>') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '>': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '>') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '?': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '[': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case ']': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '^': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '{': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '|': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '|') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '}': {
            tokEnd += 1;
            scopePosition = popScope(ScopeKind::CompoundStmt, scopePosition);
            nodeKind = NodeKind::EmptyNode;
            goto statement;
        }
        case '~': {
            tokEnd += 1;
            nodeKind = NodeKind::BitwiseNotExpr;
            goto expression;
        }
        case 'a': {
            if (std::string_view(tokEnd + 1, 7) == "nalysis" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "ssert" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "ssign" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'b': {
            if (std::string_view(tokEnd + 1, 4) == "reak" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'c': {
            if (std::string_view(tokEnd + 1, 7) == "ontinue" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'd': {
            if (std::string_view(tokEnd + 1, 1) == "o" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'e': {
            if (std::string_view(tokEnd + 1, 3) == "lif" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "lse" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'f': {
            if (std::string_view(tokEnd + 1, 2) == "or" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 6) == "orward" && !isWordBulkCharacter(tokEnd[7])) {
                tokEnd += 7;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "alse" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'g': {
            if (std::string_view(tokEnd + 1, 4) == "uard" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'i': {
            if (std::string_view(tokEnd + 1, 1) == "f" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                scopePosition = pushScope(ScopeKind::IfExprOrStmt, scopePosition);
                goto expression_continue;
            }
            if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "nout" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'l': {
            if (std::string_view(tokEnd + 1, 3) == "oop" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 2) == "et" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'm': {
            if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'n': {
            if (std::string_view(tokEnd + 1, 8) == "amespace" && !isWordBulkCharacter(tokEnd[9])) {
                tokEnd += 9;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'o': {
            if (std::string_view(tokEnd + 1, 5) == "bject" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 2) == "ut" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'r': {
            if (std::string_view(tokEnd + 1, 5) == "eturn" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 's': {
            if (std::string_view(tokEnd + 1, 5) == "truct" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "tatic" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 't': {
            if (std::string_view(tokEnd + 1, 2) == "ry" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "rait" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 7) == "emplate" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "rue" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'v': {
            if (std::string_view(tokEnd + 1, 2) == "ar" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto statement_identifier_case;
        }
        case 'w': {
            if (std::string_view(tokEnd + 1, 4) == "hile" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "ith" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
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
            if (tokEnd[0] == '\0' && tokEnd == state.sourceBufferEnd) {
                return reachedEOS(state, scopePosition);
            }
            TODO_ERROR("invalid character");
        }
        } // switch
        VERIFY_NOT_REACHED();
    statement_identifier_case:
        tokEnd = skipToEndOfIdentifier(tokEnd);
        nodeKind = NodeKind::IdentifierExpr;
        goto after_expression;
    } // retry-loop

expression:
    emitNode(nodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
expression_continue:
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        tokBegin = tokEnd;
        nodeKind = (NodeKind)0;
        data1 = 0;
    expression_dispatch:
        fmt::println("expression: {}", tokEnd[0]);
        switch (tokEnd[0]) {
        case '\n': {
            tokEnd += 1;
            continue;
        }
        case '\r': {
            if (tokEnd[1] == '\n') {
                tokEnd += 2;
                continue;
            }
            tokEnd += 1;
            continue;
        }
        case '!': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::LogicalNotExpr;
            goto expression;
        }
        case '%': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '&': {
            char next = tokEnd[1];
            if (next == '&') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '(': {
            tokEnd += 1;
            nodeKind = NodeKind::ParenthesizedExpr;
            data1 = (size_t)ScopeKind::Paren;
            goto begin_argument_scope;
        }
        case ')': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '*': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::DereferenceExpr;
            goto expression;
        }
        case '+': {
            char next = tokEnd[1];
            if (next == '+') {
                tokEnd += 2;
                nodeKind = NodeKind::PreIncrementExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::PlusExpr;
            goto expression;
        }
        case ',': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '-': {
            char next = tokEnd[1];
            if (next == '-') {
                tokEnd += 2;
                nodeKind = NodeKind::PreDecrementExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '>') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::NegateExpr;
            goto expression;
        }
        case '.': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '/': {
            char next = tokEnd[1];
            if (next == '*') {
                tokEnd += 2;
                tokEnd = skipToEndOfBlockComment(tokEnd);
                tokEnd += 2;
                emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);
                continue;
            }
            if (next == '/') {
                tokEnd += 2;
                tokEnd = skipToEndOfLine(tokEnd);
                emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);
                continue;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case ':': {
            char next = tokEnd[1];
            if (next == ':') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case ';': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '<': {
            char next = tokEnd[1];
            if (next == '<') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '=') {
                char next = tokEnd[2];
                if (next == '>') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '=': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '>') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '>': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '>') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '?': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '[': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case ']': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '^': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '{': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '|': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '|') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '}': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '~': {
            tokEnd += 1;
            nodeKind = NodeKind::BitwiseNotExpr;
            goto expression;
        }
        case 'a': {
            if (std::string_view(tokEnd + 1, 7) == "nalysis" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "ssert" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "ssign" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'b': {
            if (std::string_view(tokEnd + 1, 4) == "reak" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'c': {
            if (std::string_view(tokEnd + 1, 7) == "ontinue" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'd': {
            if (std::string_view(tokEnd + 1, 1) == "o" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'e': {
            if (std::string_view(tokEnd + 1, 3) == "lif" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "lse" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'f': {
            if (std::string_view(tokEnd + 1, 2) == "or" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 6) == "orward" && !isWordBulkCharacter(tokEnd[7])) {
                tokEnd += 7;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "alse" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'g': {
            if (std::string_view(tokEnd + 1, 4) == "uard" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'i': {
            if (std::string_view(tokEnd + 1, 1) == "f" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                scopePosition = pushScope(ScopeKind::IfExpr, scopePosition);
                goto expression_continue;
            }
            if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "nout" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'l': {
            if (std::string_view(tokEnd + 1, 3) == "oop" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 2) == "et" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'm': {
            if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'n': {
            if (std::string_view(tokEnd + 1, 8) == "amespace" && !isWordBulkCharacter(tokEnd[9])) {
                tokEnd += 9;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'o': {
            if (std::string_view(tokEnd + 1, 5) == "bject" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 2) == "ut" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'r': {
            if (std::string_view(tokEnd + 1, 5) == "eturn" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 's': {
            if (std::string_view(tokEnd + 1, 5) == "truct" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "tatic" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 't': {
            if (std::string_view(tokEnd + 1, 2) == "ry" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "rait" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 7) == "emplate" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "rue" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'v': {
            if (std::string_view(tokEnd + 1, 2) == "ar" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto expression_identifier_case;
        }
        case 'w': {
            if (std::string_view(tokEnd + 1, 4) == "hile" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "ith" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
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
            if (tokEnd[0] == '\0' && tokEnd == state.sourceBufferEnd) {
                return reachedEOS(state, scopePosition);
            }
            TODO_ERROR("invalid character");
        }
        } // switch
        VERIFY_NOT_REACHED();
    expression_identifier_case:
        tokEnd = skipToEndOfIdentifier(tokEnd);
        nodeKind = NodeKind::IdentifierExpr;
        goto after_expression;
    } // retry-loop

after_expression:
    emitNode(nodeKind, tokBegin, tokEnd, state, sourceBufferBegin);
after_expression_continue:
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        tokBegin = tokEnd;
        nodeKind = (NodeKind)0;
        data1 = 0;
    after_expression_dispatch:
        fmt::println("after_expression: {}", tokEnd[0]);
        switch (tokEnd[0]) {
        case '\n': {
            tokEnd += 1;
            continue;
        }
        case '\r': {
            if (tokEnd[1] == '\n') {
                tokEnd += 2;
                continue;
            }
            tokEnd += 1;
            continue;
        }
        case '!': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                nodeKind = NodeKind::CompareNotEqualExpr;
                goto expression;
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '%': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::RemainderExpr;
            goto expression;
        }
        case '&': {
            char next = tokEnd[1];
            if (next == '&') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                nodeKind = NodeKind::LogicalAndExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::BitwiseAndExpr;
            goto expression;
        }
        case '(': {
            tokEnd += 1;
            nodeKind = NodeKind::CallExpr;
            data1 = (size_t)ScopeKind::Paren;
            goto begin_argument_scope;
        }
        case ')': {
            tokEnd += 1;
            scopePosition = popScope(ScopeKind::Paren, scopePosition);
            nodeKind = NodeKind::EmptyNode;
            goto after_expression;
        }
        case '*': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::MultiplyExpr;
            goto expression;
        }
        case '+': {
            char next = tokEnd[1];
            if (next == '+') {
                tokEnd += 2;
                nodeKind = NodeKind::PostIncrementExpr;
                goto after_expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::AdditionExpr;
            goto expression;
        }
        case ',': {
            tokEnd += 1;
            tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
            tokBegin = tokEnd;
            if (std::string_view(tokEnd, 4) == "else" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
                tokBegin = tokEnd;
                if (std::string_view(tokEnd, 2) == "=>") {
                    tokEnd += 2;
                    nodeKind = NodeKind::CommaElseExpr;
                    goto expression;
                }
                TODO_ERROR("junk after comma-else");
            }
            if (std::string_view(tokEnd, 4) == "elif" && !isWordBulkCharacter(tokEnd[4])) {
                TODO_PARSE();
            }
            auto scopeKind = peekScope(scopePosition);
            if (isBracketScope(scopeKind)) {
                if (tokEnd[0] == scopeKindToRightBracket(scopeKind)) {
                    scopePosition = popScope(scopeKind, scopePosition);
                    tokEnd += 1;
                    nodeKind = NodeKind::EmptyNode;
                    goto after_expression;
                }
                if (isWordFirstCharacter(tokEnd[0])) {
                    goto check_for_designated_argument;
                }
                goto expression_dispatch;
            }
            TODO_ERROR("invalid scope for comma");
        }
        case '-': {
            char next = tokEnd[1];
            if (next == '-') {
                tokEnd += 2;
                nodeKind = NodeKind::PostDecrementExpr;
                goto after_expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '>') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::SubtractionExpr;
            goto expression;
        }
        case '.': {
            tokEnd += 1;
            tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
            tokBegin = tokEnd;
            if (isWordFirstCharacter(tokEnd[0])) {
                tokEnd = skipToEndOfIdentifier(tokEnd);
                nodeKind = NodeKind::MemberAccessExpr;
                goto after_expression;
            }
            TODO_ERROR("junk after access punctuation");
        }
        case '/': {
            char next = tokEnd[1];
            if (next == '*') {
                tokEnd += 2;
                tokEnd = skipToEndOfBlockComment(tokEnd);
                tokEnd += 2;
                emitNode(NodeKind::BlockComment, tokBegin, tokEnd, state, sourceBufferBegin);
                continue;
            }
            if (next == '/') {
                tokEnd += 2;
                tokEnd = skipToEndOfLine(tokEnd);
                emitNode(NodeKind::LineComment, tokBegin, tokEnd, state, sourceBufferBegin);
                continue;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::DivideExpr;
            goto expression;
        }
        case ':': {
            char next = tokEnd[1];
            if (next == ':') {
                tokEnd += 2;
                tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
                tokBegin = tokEnd;
                if (isWordFirstCharacter(tokEnd[0])) {
                    tokEnd = skipToEndOfIdentifier(tokEnd);
                    nodeKind = NodeKind::StaticAccessExpr;
                    goto after_expression;
                }
                TODO_ERROR("junk after access punctuation");
            }
            tokEnd += 1;
            auto scopeKind = peekScope(scopePosition);
            if (scopeKind == ScopeKind::IfExprOrStmt) {
                scopePosition = popScope(scopeKind, scopePosition);
                nodeKind = NodeKind::IfStmt;
                goto single_or_compound_statement;
            }
            TODO_ERROR("invalid scope for colon");
        }
        case ';': {
            tokEnd += 1;
            nodeKind = NodeKind::ExpressionStmt;
            goto statement;
        }
        case '<': {
            char next = tokEnd[1];
            if (next == '<') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                nodeKind = NodeKind::ShiftLeftExpr;
                goto expression;
            }
            if (next == '=') {
                char next = tokEnd[2];
                if (next == '>') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                nodeKind = NodeKind::CompareLessEqualExpr;
                goto expression;
            }
            tokEnd += 1;
            nodeKind = NodeKind::CompareLessExpr;
            goto expression;
        }
        case '=': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                nodeKind = NodeKind::CompareEqualExpr;
                goto expression;
            }
            if (next == '>') {
                tokEnd += 2;
                auto scopeKind = peekScope(scopePosition);
                if (scopeKind == ScopeKind::IfExpr || scopeKind == ScopeKind::IfExprOrStmt) {
                    scopePosition = popScope(scopeKind, scopePosition);
                    nodeKind = NodeKind::IfExpr;
                    goto expression;
                }
                TODO_ERROR("invalid scope for fat-arrow");
            }
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '>': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                nodeKind = NodeKind::CompareGreaterEqualExpr;
                goto expression;
            }
            if (next == '>') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                nodeKind = NodeKind::ShiftRightExpr;
                goto expression;
            }
            tokEnd += 1;
            nodeKind = NodeKind::CompareGreaterExpr;
            goto expression;
        }
        case '?': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case '[': {
            tokEnd += 1;
            nodeKind = NodeKind::IndexExpr;
            data1 = (size_t)ScopeKind::Square;
            goto begin_argument_scope;
        }
        case ']': {
            tokEnd += 1;
            scopePosition = popScope(ScopeKind::Square, scopePosition);
            nodeKind = NodeKind::EmptyNode;
            goto after_expression;
        }
        case '^': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            tokEnd += 1;
            nodeKind = NodeKind::BitwiseXorExpr;
            goto expression;
        }
        case '{': {
            tokEnd += 1;
            nodeKind = NodeKind::Parameterize;
            data1 = (size_t)ScopeKind::Brace;
            goto begin_argument_scope;
        }
        case '|': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (next == '|') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO_ERROR("invalid token for state");
                }
                tokEnd += 2;
                nodeKind = NodeKind::LogicalOrExpr;
                goto expression;
            }
            tokEnd += 1;
            nodeKind = NodeKind::BitwiseOrExpr;
            goto expression;
        }
        case '}': {
            tokEnd += 1;
            scopePosition = popScope(ScopeKind::Brace, scopePosition);
            nodeKind = NodeKind::EmptyNode;
            goto after_expression;
        }
        case '~': {
            tokEnd += 1;
            TODO_ERROR("invalid token for state");
        }
        case 'a': {
            if (std::string_view(tokEnd + 1, 7) == "nalysis" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "ssert" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "ssign" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'b': {
            if (std::string_view(tokEnd + 1, 4) == "reak" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'c': {
            if (std::string_view(tokEnd + 1, 7) == "ontinue" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'd': {
            if (std::string_view(tokEnd + 1, 1) == "o" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'e': {
            if (std::string_view(tokEnd + 1, 3) == "lif" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "lse" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'f': {
            if (std::string_view(tokEnd + 1, 2) == "or" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 6) == "orward" && !isWordBulkCharacter(tokEnd[7])) {
                tokEnd += 7;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "alse" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'g': {
            if (std::string_view(tokEnd + 1, 4) == "uard" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'i': {
            if (std::string_view(tokEnd + 1, 1) == "f" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 1) == "n" && !isWordBulkCharacter(tokEnd[2])) {
                tokEnd += 2;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "nout" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'l': {
            if (std::string_view(tokEnd + 1, 3) == "oop" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 2) == "et" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'm': {
            if (std::string_view(tokEnd + 1, 4) == "atch" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'n': {
            if (std::string_view(tokEnd + 1, 8) == "amespace" && !isWordBulkCharacter(tokEnd[9])) {
                tokEnd += 9;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'o': {
            if (std::string_view(tokEnd + 1, 5) == "bject" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 2) == "ut" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'r': {
            if (std::string_view(tokEnd + 1, 5) == "eturn" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 's': {
            if (std::string_view(tokEnd + 1, 5) == "truct" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 5) == "tatic" && !isWordBulkCharacter(tokEnd[6])) {
                tokEnd += 6;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 't': {
            if (std::string_view(tokEnd + 1, 2) == "ry" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 4) == "rait" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 7) == "emplate" && !isWordBulkCharacter(tokEnd[8])) {
                tokEnd += 8;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "rue" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'v': {
            if (std::string_view(tokEnd + 1, 2) == "ar" && !isWordBulkCharacter(tokEnd[3])) {
                tokEnd += 3;
                TODO_ERROR("invalid token for state");
            }
            goto after_expression_identifier_case;
        }
        case 'w': {
            if (std::string_view(tokEnd + 1, 4) == "hile" && !isWordBulkCharacter(tokEnd[5])) {
                tokEnd += 5;
                TODO_ERROR("invalid token for state");
            }
            if (std::string_view(tokEnd + 1, 3) == "ith" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                TODO_ERROR("invalid token for state");
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
            if (tokEnd[0] == '\0' && tokEnd == state.sourceBufferEnd) {
                return reachedEOS(state, scopePosition);
            }
            TODO_ERROR("invalid character");
        }
        } // switch
        VERIFY_NOT_REACHED();
    after_expression_identifier_case:
        tokEnd = skipToEndOfIdentifier(tokEnd);
        TODO_ERROR("invalid token for state");
    } // retry-loop
}

std::string_view nameString(NodeKind kind) {
    switch (kind) {
#define NODE(kind, type, prec) \
    case NodeKind::kind:       \
        return #kind;

#include "nodes.inc"
    }
}