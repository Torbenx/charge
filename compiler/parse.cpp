
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
    std::vector<ScopeKind> scopes;
    WordStringTable wordTable { words };
    const char* sourceBufferEnd;

    ParseStackState(std::string_view sourceBuf)
        : sourceBufferEnd(sourceBuf.end()) { }
};

static void beginScope(ScopeKind kind, std::vector<ScopeKind>& scopes) {
    scopes.push_back(kind);
}

static void endScope(ScopeKind kind, std::vector<ScopeKind>& scopes) {
    VERIFY(!scopes.empty());
    VERIFY(scopes.back() == kind);
    scopes.pop_back();
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

static std::vector<Node> reachedEOS(ParseStackState& state) {
    VERIFY(state.scopes.empty());
    return state.nodes;
}

std::vector<Node> parse(std::string_view sourceBuf) {
    ParseStackState state(sourceBuf);
    const char* sourceBufferBegin = sourceBuf.begin();
    const char* tokBegin = sourceBufferBegin;
    const char* tokEnd = sourceBufferBegin;
    NodeKind tokKind = (NodeKind)0;
    size_t data1 = 0;

    tokKind = NodeKind::Newline;
    goto expression;

#define TODO() VERIFY_NOT_REACHED()

check_for_designated_argument : {
    Word word;
    tokEnd = readWord(tokEnd, word, state.wordTable);
    auto savedBegin = tokBegin;
    auto savedEnd = tokEnd;
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    if (std::string_view(tokEnd, 1) == "=") {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            tokKind = NodeKind::DesignateArgument;
            goto expression;
        }
    }
    emitNode(NodeKind::IdentifierExpr, savedBegin, savedEnd, state, sourceBufferBegin);
    goto after_expression_dispatch;
}
begin_argument_scope : {
    emitNode(tokKind, tokBegin, tokEnd, state, sourceBufferBegin);
    ScopeKind scopeKind = (ScopeKind)data1;
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    if (tokEnd[0] == scopeKindToRightBracket(scopeKind)) {
        tokEnd += 1;
        tokKind = NodeKind::EmptyNode;
        goto after_expression;
    }
    beginScope(scopeKind, state.scopes);
    if (isWordFirstCharacter(tokEnd[0])) {
        goto check_for_designated_argument;
    }
    goto expression_dispatch;
}
single_or_compound_statement: {
    emitNode(tokKind, tokBegin, tokEnd, state, sourceBufferBegin);
    tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
    if (std::string_view(tokEnd, 1) == "{") {
        tokEnd += 1;
        beginScope(ScopeKind::CompoundStmt, state.scopes);
        tokKind = NodeKind::CompoundStmt;
        goto statement;
    }
    goto statement_dispatch;
}

statement:
    emitNode(tokKind, tokBegin, tokEnd, state, sourceBufferBegin);
statement_continue:
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        tokBegin = tokEnd;
        tokKind = (NodeKind)0;
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
            tokKind = NodeKind::LogicalNotExpr;
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
            tokKind = NodeKind::ParenthesizedExpr;
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
            tokKind = NodeKind::DereferenceExpr;
            goto expression;
        }
        case '+': {
            char next = tokEnd[1];
            if (next == '+') {
                tokEnd += 2;
                tokKind = NodeKind::PreIncrementExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::PlusExpr;
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
                tokKind = NodeKind::PreDecrementExpr;
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
            tokKind = NodeKind::NegateExpr;
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
            endScope(ScopeKind::CompoundStmt, state.scopes);
            tokKind = NodeKind::EmptyNode;
            goto statement;
        }
        case '~': {
            tokEnd += 1;
            tokKind = NodeKind::BitwiseNotExpr;
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
                beginScope(ScopeKind::IfExprOrStmt, state.scopes);
                goto expression_continue;
            }
            if (word == words["if"]) {
                beginScope(ScopeKind::IfExpr, state.scopes);
                continue;
            }
            tokKind = NodeKind::IdentifierExpr;
            goto after_expression;
        }
        default: {
            if (tokEnd[0] == '\0' && tokEnd == state.sourceBufferEnd) {
                return reachedEOS(state);
            }
            TODO();
        }
        } // switch
        VERIFY_NOT_REACHED();
    } // retry-loop

expression:
    emitNode(tokKind, tokBegin, tokEnd, state, sourceBufferBegin);
expression_continue:
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        tokBegin = tokEnd;
        tokKind = (NodeKind)0;
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
            tokKind = NodeKind::LogicalNotExpr;
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
            tokKind = NodeKind::ParenthesizedExpr;
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
            tokKind = NodeKind::DereferenceExpr;
            goto expression;
        }
        case '+': {
            char next = tokEnd[1];
            if (next == '+') {
                tokEnd += 2;
                tokKind = NodeKind::PreIncrementExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::PlusExpr;
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
                tokKind = NodeKind::PreDecrementExpr;
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
            tokKind = NodeKind::NegateExpr;
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
            tokKind = NodeKind::BitwiseNotExpr;
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
                beginScope(ScopeKind::IfExpr, state.scopes);
                continue;
            }
            tokKind = NodeKind::IdentifierExpr;
            goto after_expression;
        }
        default: {
            if (tokEnd[0] == '\0' && tokEnd == state.sourceBufferEnd) {
                return reachedEOS(state);
            }
            TODO();
        }
        } // switch
        VERIFY_NOT_REACHED();
    } // retry-loop

after_expression:
    emitNode(tokKind, tokBegin, tokEnd, state, sourceBufferBegin);
after_expression_continue:
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        tokBegin = tokEnd;
        tokKind = (NodeKind)0;
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
                tokKind = NodeKind::CompareNotEqualExpr;
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
            tokKind = NodeKind::RemainderExpr;
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
                tokKind = NodeKind::LogicalAndExpr;
                goto expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::BitwiseAndExpr;
            goto expression;
        }
        case '(': {
            tokEnd += 1;
            tokKind = NodeKind::CallExpr;
            data1 = (size_t)ScopeKind::Paren;
            goto begin_argument_scope;
        }
        case ')': {
            tokEnd += 1;
            endScope(ScopeKind::Paren, state.scopes);
            tokKind = NodeKind::EmptyNode;
            goto after_expression;
        }
        case '*': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::MultiplyExpr;
            goto expression;
        }
        case '+': {
            char next = tokEnd[1];
            if (next == '+') {
                tokEnd += 2;
                tokKind = NodeKind::PostIncrementExpr;
                goto after_expression;
            }
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::AdditionExpr;
            goto expression;
        }
        case ',': {
            tokEnd += 1;
            tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
            if (std::string_view(tokEnd, 4) == "else" && !isWordBulkCharacter(tokEnd[4])) {
                tokEnd += 4;
                tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
                if (std::string_view(tokEnd, 2) == "=>") {
                    tokEnd += 2;
                    tokKind = NodeKind::CommaElseExpr;
                    goto expression;
                }
                TODO();
            }
            if (std::string_view(tokEnd, 4) == "elif" && !isWordBulkCharacter(tokEnd[4])) {
                TODO();
            }
            if (!state.scopes.empty()) {
                auto scopeKind = state.scopes.back();
                if (isBracketScope(scopeKind)) {
                    if (tokEnd[0] == scopeKindToRightBracket(scopeKind)) {
                        endScope(scopeKind, state.scopes);
                        tokEnd += 1;
                        tokKind = NodeKind::EmptyNode;
                        goto after_expression;
                    }
                    if (isWordFirstCharacter(tokEnd[0])) {
                        goto check_for_designated_argument;
                    }
                    goto expression_dispatch;
                }
            }
            TODO();
        }
        case '-': {
            char next = tokEnd[1];
            if (next == '-') {
                tokEnd += 2;
                tokKind = NodeKind::PostDecrementExpr;
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
            tokKind = NodeKind::SubtractionExpr;
            goto expression;
        }
        case '.': {
            tokEnd += 1;
            tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
            if (isWordFirstCharacter(tokEnd[0])) {
                Word word;
                tokEnd = readWord(tokEnd, word, state.wordTable);
                tokKind = NodeKind::MemberAccessExpr;
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
            tokKind = NodeKind::DivideExpr;
            goto expression;
        }
        case ':': {
            char next = tokEnd[1];
            if (next == ':') {
                tokEnd += 2;
                tokEnd = inlineAdvancer(tokEnd, state, sourceBufferBegin);
                if (isWordFirstCharacter(tokEnd[0])) {
                    Word word;
                    tokEnd = readWord(tokEnd, word, state.wordTable);
                    tokKind = NodeKind::StaticAccessExpr;
                    goto after_expression;
                }
                TODO();
            }
            tokEnd += 1;
            auto scopeKind = state.scopes.back();
            if (scopeKind == ScopeKind::IfExprOrStmt) {
                endScope(scopeKind, state.scopes);
                tokKind = NodeKind::IfStmt;
                goto single_or_compound_statement;
            }
            TODO();
        }
        case ';': {
            tokEnd += 1;
            tokKind = NodeKind::ExpressionStmt;
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
                tokKind = NodeKind::ShiftLeftExpr;
                goto expression;
            }
            if (next == '=') {
                char next = tokEnd[2];
                if (next == '>') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                tokKind = NodeKind::CompareLessEqualExpr;
                goto expression;
            }
            tokEnd += 1;
            tokKind = NodeKind::CompareLessExpr;
            goto expression;
        }
        case '=': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                tokKind = NodeKind::CompareEqualExpr;
                goto expression;
            }
            if (next == '>') {
                tokEnd += 2;
                auto scopeKind = state.scopes.back();
                if (scopeKind == ScopeKind::IfExpr || scopeKind == ScopeKind::IfExprOrStmt) {
                    endScope(scopeKind, state.scopes);
                    tokKind = NodeKind::IfExpr;
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
                tokKind = NodeKind::CompareGreaterEqualExpr;
                goto expression;
            }
            if (next == '>') {
                char next = tokEnd[2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                tokKind = NodeKind::ShiftRightExpr;
                goto expression;
            }
            tokEnd += 1;
            tokKind = NodeKind::CompareGreaterExpr;
            goto expression;
        }
        case '?': {
            tokEnd += 1;
            TODO();
        }
        case '[': {
            tokEnd += 1;
            tokKind = NodeKind::IndexExpr;
            data1 = (size_t)ScopeKind::Square;
            goto begin_argument_scope;
        }
        case ']': {
            tokEnd += 1;
            endScope(ScopeKind::Square, state.scopes);
            tokKind = NodeKind::EmptyNode;
            goto after_expression;
        }
        case '^': {
            char next = tokEnd[1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::BitwiseXorExpr;
            goto expression;
        }
        case '{': {
            tokEnd += 1;
            tokKind = NodeKind::Parameterize;
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
                tokKind = NodeKind::LogicalOrExpr;
                goto expression;
            }
            tokEnd += 1;
            tokKind = NodeKind::BitwiseOrExpr;
            goto expression;
        }
        case '}': {
            tokEnd += 1;
            endScope(ScopeKind::Brace, state.scopes);
            tokKind = NodeKind::EmptyNode;
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
                return reachedEOS(state);
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