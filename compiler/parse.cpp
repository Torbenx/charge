
#include "WordTable.h"
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
    WordStringTable wordTable { words };
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

[[nodiscard]] static const char* readWord(const char* position, Word& outWord, WordStringTable& wordTable) {
    const char* wordStart = position;
    uint32_t hash = 0;
    do {
        hash = Word::iterateHash(hash, position[0]);
        position += 1;
    } while (isWordBulkCharacter(position[0]));
    hash = Word::finalizeHash(hash);
    outWord = wordTable.getWithHash(std::string_view(wordStart, position), hash);
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
    ScopeKind* scopePosition = new (std::align_val_t(SCOPE_BUFFER_SIZE)) ScopeKind[SCOPE_BUFFER_SIZE];
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

#define TODO() VERIFY_NOT_REACHED()

check_for_designated_argument : {
    Word word;
    tokEnd = readWord(tokEnd, word, state.wordTable);
    auto savedBegin = tokBegin;
    auto savedEnd = tokEnd;
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    tokBegin = tokEnd;
    if (std::string_view(tokEnd, 1) == "=") {
        char next = tokEnd[1];
        if (next != '>' && next != '=') {
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
                TODO();
            }
            tokEnd += 1;
            nodeKind = NodeKind::LogicalNotExpr;
            goto expression;
        }
        case '%': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '&': {
            char next = tokEnd[1];
            if (next == '&') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '(': {
            tokEnd += 1;
            nodeKind = NodeKind::ParenthesizedExpr;
            data1 = (size_t)ScopeKind::Paren;
            goto begin_argument_scope;
        }
        case ')': {
            tokEnd += 1;
            TODO();
        }
        case '*': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
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
                TODO();
            }
            tokEnd += 1;
            nodeKind = NodeKind::PlusExpr;
            goto expression;
        }
        case ',': {
            tokEnd += 1;
            TODO();
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
                TODO();
            }
            if (next == '>') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            nodeKind = NodeKind::NegateExpr;
            goto expression;
        }
        case '.': {
            tokEnd += 1;
            TODO();
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
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case ':': {
            char next = tokEnd[1];
            if (next == ':') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case ';': {
            tokEnd += 1;
            TODO();
        }
        case '<': {
            char next = tokEnd[1];
            if (next == '<') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            if (next == '=') {
                char next = tokEnd[2];
                if (next == '>') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '=': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '>') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '>': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '>') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '?': {
            tokEnd += 1;
            TODO();
        }
        case '[': {
            tokEnd += 1;
            TODO();
        }
        case ']': {
            tokEnd += 1;
            TODO();
        }
        case '^': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '{': {
            tokEnd += 1;
            TODO();
        }
        case '|': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '|') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '}': {
            tokEnd += 1;
            scopePosition = popScope(ScopeKind::CompoundStmt, scopePosition);
            nodeKind = NodeKind::EmptyNode;;
            goto statement;
        }
        case '~': {
            tokEnd += 1;
            nodeKind = NodeKind::BitwiseNotExpr;
            goto expression;
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
        case '_': {
            Word word;
            tokEnd = readWord(tokEnd, word, state.wordTable);
            if (word == words["if"]) {
                scopePosition = pushScope(ScopeKind::IfExprOrStmt, scopePosition);
                goto expression_continue;
            }
            if (word == words["if"]) {
                scopePosition = pushScope(ScopeKind::IfExpr, scopePosition);
                continue;
            }
            nodeKind = NodeKind::IdentifierExpr;
            goto after_expression;
        }
        default: {
            if (tokEnd[0] == '\0' && tokEnd == state.sourceBufferEnd) {
                return reachedEOS(state, scopePosition);
            }
            TODO();
        }
        } // switch
        VERIFY_NOT_REACHED();
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
                TODO();
            }
            tokEnd += 1;
            nodeKind = NodeKind::LogicalNotExpr;
            goto expression;
        }
        case '%': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '&': {
            char next = tokEnd[1];
            if (next == '&') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '(': {
            tokEnd += 1;
            nodeKind = NodeKind::ParenthesizedExpr;
            data1 = (size_t)ScopeKind::Paren;
            goto begin_argument_scope;
        }
        case ')': {
            tokEnd += 1;
            TODO();
        }
        case '*': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
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
                TODO();
            }
            tokEnd += 1;
            nodeKind = NodeKind::PlusExpr;
            goto expression;
        }
        case ',': {
            tokEnd += 1;
            TODO();
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
                TODO();
            }
            if (next == '>') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            nodeKind = NodeKind::NegateExpr;
            goto expression;
        }
        case '.': {
            tokEnd += 1;
            TODO();
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
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case ':': {
            char next = tokEnd[1];
            if (next == ':') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case ';': {
            tokEnd += 1;
            TODO();
        }
        case '<': {
            char next = tokEnd[1];
            if (next == '<') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            if (next == '=') {
                char next = tokEnd[2];
                if (next == '>') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '=': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '>') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '>': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '>') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '?': {
            tokEnd += 1;
            TODO();
        }
        case '[': {
            tokEnd += 1;
            TODO();
        }
        case ']': {
            tokEnd += 1;
            TODO();
        }
        case '^': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '{': {
            tokEnd += 1;
            TODO();
        }
        case '|': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '|') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '}': {
            tokEnd += 1;
            TODO();
        }
        case '~': {
            tokEnd += 1;
            nodeKind = NodeKind::BitwiseNotExpr;
            goto expression;
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
        case '_': {
            Word word;
            tokEnd = readWord(tokEnd, word, state.wordTable);
            if (word == words["if"]) {
                scopePosition = pushScope(ScopeKind::IfExpr, scopePosition);
                continue;
            }
            nodeKind = NodeKind::IdentifierExpr;
            goto after_expression;
        }
        default: {
            if (tokEnd[0] == '\0' && tokEnd == state.sourceBufferEnd) {
                return reachedEOS(state, scopePosition);
            }
            TODO();
        }
        } // switch
        VERIFY_NOT_REACHED();
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
            TODO();
        }
        case '%': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
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
                    TODO();
                }
                tokEnd += 2;
                nodeKind = NodeKind::LogicalAndExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO();
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
                TODO();
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
                TODO();
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
                TODO();
            }
            if (std::string_view(tokEnd, 4) == "elif" && !isWordBulkCharacter(tokEnd[4])) {
                TODO();
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
            TODO();
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
                TODO();
            }
            if (next == '>') {
                tokEnd += 2;
                TODO();
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
                Word word;
                tokEnd = readWord(tokEnd, word, state.wordTable);
                nodeKind = NodeKind::MemberAccessExpr;
                goto after_expression;
            }
            TODO();
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
                TODO();
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
                    Word word;
                    tokEnd = readWord(tokEnd, word, state.wordTable);
                    nodeKind = NodeKind::StaticAccessExpr;
                    goto after_expression;
                }
                TODO();
            }
            tokEnd += 1;
            auto scopeKind = peekScope(scopePosition);
            if (scopeKind == ScopeKind::IfExprOrStmt) {
                scopePosition = popScope(scopeKind, scopePosition);
                nodeKind = NodeKind::IfStmt;
                goto single_or_compound_statement;
            }
            TODO();
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
                    TODO();
                }
                tokEnd += 2;
                nodeKind = NodeKind::ShiftLeftExpr;
                goto expression;
            }
            if (next == '=') {
                char next = tokEnd[2];
                if (next == '>') {
                    tokEnd += 3;
                    TODO();
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
                TODO();
            }
            tokEnd += 1;
            TODO();
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
                    TODO();
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
            TODO();
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
                TODO();
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
                TODO();
            }
            if (next == '|') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
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
            TODO();
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
        case '_': {
            Word word;
            tokEnd = readWord(tokEnd, word, state.wordTable);
            TODO();
        }
        default: {
            if (tokEnd[0] == '\0' && tokEnd == state.sourceBufferEnd) {
                return reachedEOS(state, scopePosition);
            }
            TODO();
        }
        } // switch
        VERIFY_NOT_REACHED();
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