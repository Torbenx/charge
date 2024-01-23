
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
struct Scope {
    ScopeKind kind;
    size_t beginNode;
};

std::vector<Node> parse(std::string_view sourceBuffer) {
    int_t tokBegin = 0;
    int_t tokEnd = 0;
    NodeKind tokKind = (NodeKind)0;
    size_t data1 = 0;

    std::vector<Node> nodes;
    std::vector<Scope> scopes;

    WordStringTable wordTable { words };

    auto beginScope = [&](ScopeKind kind) {
        scopes.push_back({ kind, nodes.size() });
    };
    auto endScope = [&](ScopeKind kind) {
        VERIFY(!scopes.empty());
        VERIFY(scopes.back().kind == kind);
        scopes.pop_back();
    };
    auto reachedEOS = [&]() {
        VERIFY(scopes.empty());
        return nodes;
    };

    auto readWord = [&]() {
        uint32_t hash = 0;
        do {
            hash = Word::iterateHash(hash, sourceBuffer[tokEnd]);
            tokEnd += 1;
        } while (isWordBulkCharacter(sourceBuffer[tokEnd]));
        hash = Word::finalizeHash(hash);
        return wordTable.getWithHash(sourceBuffer.substr(tokBegin, tokEnd - tokBegin), hash);
    };

    auto skipWhitespace = [&]() {
        while (sourceBuffer[tokEnd] == ' ' || sourceBuffer[tokEnd] == '\t')
            tokEnd += 1;
    };

    // advances offset to the next '*/'
    auto skipToEndOfBlockComment = [&]() {
        while (tokEnd < (int_t)sourceBuffer.size()
            && !(sourceBuffer[tokEnd] == '*' && sourceBuffer[tokEnd + 1] == '/')) {
            tokEnd += 1;
        }
    };

    // advances offset to the next new line character
    auto skipToEndOfLine = [&]() {
        while (tokEnd < (int_t)sourceBuffer.size()
            && sourceBuffer[tokEnd] != '\n' && sourceBuffer[tokEnd] != '\r') {
            tokEnd += 1;
        }
    };

    auto skipToEndOfCharacterLiteral = [&]() {
        while (tokEnd < (int_t)sourceBuffer.size() && sourceBuffer[tokEnd] != '\''
            && sourceBuffer[tokEnd] != '\n' && sourceBuffer[tokEnd] != '\r') {
            tokEnd += 1;
        }
    };

    tokKind = NodeKind::Newline;
    goto expression;

#define TODO() VERIFY_NOT_REACHED()

immediate_right_bracket_or_expression : {
    nodes.push_back({ tokKind, (uint32_t)tokBegin, (uint32_t)tokEnd });
    ScopeKind scopeKind = (ScopeKind)data1;
    for (;;) {
        skipWhitespace();
        tokBegin = tokEnd;
        tokKind = (NodeKind)0;
        data1 = 0;
        if (sourceBuffer[tokEnd] == '/') {
            if (sourceBuffer[tokEnd + 1] == '/') {
                tokEnd += 2;
                skipToEndOfLine();
                nodes.push_back({ NodeKind::LineComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                continue;
            }
            if (sourceBuffer[tokEnd + 1] == '*') {
                tokEnd += 2;
                skipToEndOfBlockComment();
                tokEnd += 2;
                nodes.push_back({ NodeKind::BlockComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                continue;
            }
        }
        if (sourceBuffer[tokEnd] == '\r') {
            tokEnd += 1;
            continue;
        }
        if (sourceBuffer[tokEnd] == '\n') {
            if (sourceBuffer[tokEnd + 1] == '\n') {
                tokEnd += 2;
                continue;
            }
            tokEnd += 1;
            continue;
        }
        break;
    } // retry-loop
    if (sourceBuffer[tokEnd] == scopeKindToRightBracket(scopeKind)) {
        tokEnd += 1;
        tokKind = NodeKind::EmptyNode;
        goto after_expression;
    }
    beginScope(scopeKind);
    goto expression_dispatch;
}

expression:
    nodes.push_back({ tokKind, (uint32_t)tokBegin, (uint32_t)tokEnd });
    for (;;) {
        skipWhitespace();
        tokBegin = tokEnd;
        tokKind = (NodeKind)0;
        data1 = 0;
    expression_dispatch:
        fmt::println("expression: {}", sourceBuffer.substr(tokEnd, 1));
        switch (sourceBuffer[tokEnd]) {
        case '\n': {
            tokEnd += 1;
            continue;
        }
        case '\r': {
            if (sourceBuffer[tokEnd + 1] == '\n') {
                tokEnd += 2;
                continue;
            }
            tokEnd += 1;
            continue;
        }
        case '!': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::LogicalNotExpr;
            goto expression;
        }
        case '%': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case '&': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '&') {
                char next = sourceBuffer[tokEnd + 2];
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
            goto immediate_right_bracket_or_expression;
        }
        case ')': {
            tokEnd += 1;
            TODO();
        }
        case '*': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::DereferenceExpr;
            goto expression;
        }
        case '+': {
            char next = sourceBuffer[tokEnd + 1];
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
            char next = sourceBuffer[tokEnd + 1];
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
            char next = sourceBuffer[tokEnd + 1];
            if (next == '*') {
                tokEnd += 2;
                skipToEndOfBlockComment();
                tokEnd += 2;
                nodes.push_back({ NodeKind::BlockComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                continue;
            }
            if (next == '/') {
                tokEnd += 2;
                skipToEndOfLine();
                nodes.push_back({ NodeKind::LineComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
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
            char next = sourceBuffer[tokEnd + 1];
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
            char next = sourceBuffer[tokEnd + 1];
            if (next == '<') {
                char next = sourceBuffer[tokEnd + 2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                TODO();
            }
            if (next == '=') {
                char next = sourceBuffer[tokEnd + 2];
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
            char next = sourceBuffer[tokEnd + 1];
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
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '>') {
                char next = sourceBuffer[tokEnd + 2];
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
            char next = sourceBuffer[tokEnd + 1];
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
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '|') {
                char next = sourceBuffer[tokEnd + 2];
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
            auto word = readWord();
            if (word == words["if"]) {
                beginScope(ScopeKind::IfExpr);
                continue;
            }
            tokKind = NodeKind::IdentifierExpr;
            goto after_expression;
        }
        default: {
            if (sourceBuffer[tokEnd] == '\0' && tokEnd == (int_t)sourceBuffer.length()) {
                return reachedEOS();
            }
            TODO();
        }
        } // switch
        VERIFY_NOT_REACHED();
    } // retry-loop

after_expression:
    nodes.push_back({ tokKind, (uint32_t)tokBegin, (uint32_t)tokEnd });
    for (;;) {
        skipWhitespace();
        tokBegin = tokEnd;
        tokKind = (NodeKind)0;
        data1 = 0;
    after_expression_dispatch:
        fmt::println("after_expression: {}", sourceBuffer.substr(tokEnd, 1));
        switch (sourceBuffer[tokEnd]) {
        case '\n': {
            tokEnd += 1;
            continue;
        }
        case '\r': {
            if (sourceBuffer[tokEnd + 1] == '\n') {
                tokEnd += 2;
                continue;
            }
            tokEnd += 1;
            continue;
        }
        case '!': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                tokKind = NodeKind::CompareNotEqualExpr;
                goto expression;
            }
            tokEnd += 1;
            TODO();
        }
        case '%': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::RemainderExpr;
            goto expression;
        }
        case '&': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '&') {
                char next = sourceBuffer[tokEnd + 2];
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
            goto immediate_right_bracket_or_expression;
        }
        case ')': {
            tokEnd += 1;
            endScope(ScopeKind::Paren);
            tokKind = NodeKind::EmptyNode;
            goto after_expression;
        }
        case '*': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            tokEnd += 1;
            tokKind = NodeKind::MultiplyExpr;
            goto expression;
        }
        case '+': {
            char next = sourceBuffer[tokEnd + 1];
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
            for (;;) {
                skipWhitespace();
                tokBegin = tokEnd;
                tokKind = (NodeKind)0;
                data1 = 0;
                if (sourceBuffer[tokEnd] == '/') {
                    if (sourceBuffer[tokEnd + 1] == '/') {
                        tokEnd += 2;
                        skipToEndOfLine();
                        nodes.push_back({ NodeKind::LineComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                        continue;
                    }
                    if (sourceBuffer[tokEnd + 1] == '*') {
                        tokEnd += 2;
                        skipToEndOfBlockComment();
                        tokEnd += 2;
                        nodes.push_back({ NodeKind::BlockComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                        continue;
                    }
                }
                if (sourceBuffer[tokEnd] == '\r') {
                    tokEnd += 1;
                    continue;
                }
                if (sourceBuffer[tokEnd] == '\n') {
                    if (sourceBuffer[tokEnd + 1] == '\n') {
                        tokEnd += 2;
                        continue;
                    }
                    tokEnd += 1;
                    continue;
                }
                break;
            } // retry-loop
            if (sourceBuffer.substr(tokEnd, 4) == "else" && !isWordBulkCharacter(sourceBuffer[tokEnd + 4])) {
                tokEnd += 4;
                for (;;) {
                    skipWhitespace();
                    tokBegin = tokEnd;
                    tokKind = (NodeKind)0;
                    data1 = 0;
                    if (sourceBuffer[tokEnd] == '/') {
                        if (sourceBuffer[tokEnd + 1] == '/') {
                            tokEnd += 2;
                            skipToEndOfLine();
                            nodes.push_back({ NodeKind::LineComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                            continue;
                        }
                        if (sourceBuffer[tokEnd + 1] == '*') {
                            tokEnd += 2;
                            skipToEndOfBlockComment();
                            tokEnd += 2;
                            nodes.push_back({ NodeKind::BlockComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                            continue;
                        }
                    }
                    if (sourceBuffer[tokEnd] == '\r') {
                        tokEnd += 1;
                        continue;
                    }
                    if (sourceBuffer[tokEnd] == '\n') {
                        if (sourceBuffer[tokEnd + 1] == '\n') {
                            tokEnd += 2;
                            continue;
                        }
                        tokEnd += 1;
                        continue;
                    }
                    break;
                } // retry-loop
                if (sourceBuffer.substr(tokEnd, 2) == "=>") {
                    tokEnd += 2;
                    tokKind = NodeKind::CommaElseExpr;
                    goto expression;
                }
                TODO();
            }
            if (sourceBuffer.substr(tokEnd, 4) == "elif" && !isWordBulkCharacter(sourceBuffer[tokEnd + 4])) {
                TODO();
            }
            if (!scopes.empty()) {
                auto scopeKind = scopes.back().kind;
                if (isBracketScope(scopeKind)) {
                    if (sourceBuffer[tokEnd] == scopeKindToRightBracket(scopeKind)) {
                        endScope(scopeKind);
                        tokEnd += 1;
                        tokKind = NodeKind::EmptyNode;
                        goto after_expression;
                    }
                    goto expression_dispatch;
                }
            }
            TODO();
        }
        case '-': {
            char next = sourceBuffer[tokEnd + 1];
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
            for (;;) {
                skipWhitespace();
                tokBegin = tokEnd;
                tokKind = (NodeKind)0;
                data1 = 0;
                if (sourceBuffer[tokEnd] == '/') {
                    if (sourceBuffer[tokEnd + 1] == '/') {
                        tokEnd += 2;
                        skipToEndOfLine();
                        nodes.push_back({ NodeKind::LineComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                        continue;
                    }
                    if (sourceBuffer[tokEnd + 1] == '*') {
                        tokEnd += 2;
                        skipToEndOfBlockComment();
                        tokEnd += 2;
                        nodes.push_back({ NodeKind::BlockComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                        continue;
                    }
                }
                if (sourceBuffer[tokEnd] == '\r') {
                    tokEnd += 1;
                    continue;
                }
                if (sourceBuffer[tokEnd] == '\n') {
                    if (sourceBuffer[tokEnd + 1] == '\n') {
                        tokEnd += 2;
                        continue;
                    }
                    tokEnd += 1;
                    continue;
                }
                break;
            } // retry-loop
            if (isWordFirstCharacter(sourceBuffer[tokEnd])) {
                auto word = readWord();
                tokKind = NodeKind::MemberAccessExpr;
                goto after_expression;
            }
            TODO();
        }
        case '/': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '*') {
                tokEnd += 2;
                skipToEndOfBlockComment();
                tokEnd += 2;
                nodes.push_back({ NodeKind::BlockComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                continue;
            }
            if (next == '/') {
                tokEnd += 2;
                skipToEndOfLine();
                nodes.push_back({ NodeKind::LineComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
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
            char next = sourceBuffer[tokEnd + 1];
            if (next == ':') {
                tokEnd += 2;
                for (;;) {
                    skipWhitespace();
                    tokBegin = tokEnd;
                    tokKind = (NodeKind)0;
                    data1 = 0;
                    if (sourceBuffer[tokEnd] == '/') {
                        if (sourceBuffer[tokEnd + 1] == '/') {
                            tokEnd += 2;
                            skipToEndOfLine();
                            nodes.push_back({ NodeKind::LineComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                            continue;
                        }
                        if (sourceBuffer[tokEnd + 1] == '*') {
                            tokEnd += 2;
                            skipToEndOfBlockComment();
                            tokEnd += 2;
                            nodes.push_back({ NodeKind::BlockComment, (uint32_t)tokBegin, (uint32_t)tokEnd });
                            continue;
                        }
                    }
                    if (sourceBuffer[tokEnd] == '\r') {
                        tokEnd += 1;
                        continue;
                    }
                    if (sourceBuffer[tokEnd] == '\n') {
                        if (sourceBuffer[tokEnd + 1] == '\n') {
                            tokEnd += 2;
                            continue;
                        }
                        tokEnd += 1;
                        continue;
                    }
                    break;
                } // retry-loop
                if (isWordFirstCharacter(sourceBuffer[tokEnd])) {
                    auto word = readWord();
                    tokKind = NodeKind::StaticAccessExpr;
                    goto after_expression;
                }
                TODO();
            }
            tokEnd += 1;
            TODO();
        }
        case ';': {
            tokEnd += 1;
            tokKind = NodeKind::ExpressionStmt;
            goto expression;
        }
        case '<': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '<') {
                char next = sourceBuffer[tokEnd + 2];
                if (next == '=') {
                    tokEnd += 3;
                    TODO();
                }
                tokEnd += 2;
                tokKind = NodeKind::ShiftLeftExpr;
                goto expression;
            }
            if (next == '=') {
                char next = sourceBuffer[tokEnd + 2];
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
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                tokKind = NodeKind::CompareEqualExpr;
                goto expression;
            }
            if (next == '>') {
                tokEnd += 2;
                endScope(ScopeKind::IfExpr);
                tokKind = NodeKind::IfExpr;
                goto expression;
            }
            tokEnd += 1;
            TODO();
        }
        case '>': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                tokKind = NodeKind::CompareGreaterEqualExpr;
                goto expression;
            }
            if (next == '>') {
                char next = sourceBuffer[tokEnd + 2];
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
            goto immediate_right_bracket_or_expression;
        }
        case ']': {
            tokEnd += 1;
            endScope(ScopeKind::Square);
            tokKind = NodeKind::EmptyNode;
            goto after_expression;
        }
        case '^': {
            char next = sourceBuffer[tokEnd + 1];
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
            goto immediate_right_bracket_or_expression;
        }
        case '|': {
            char next = sourceBuffer[tokEnd + 1];
            if (next == '=') {
                tokEnd += 2;
                TODO();
            }
            if (next == '|') {
                char next = sourceBuffer[tokEnd + 2];
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
            endScope(ScopeKind::Brace);
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
            auto word = readWord();
            TODO();
        }
        default: {
            if (sourceBuffer[tokEnd] == '\0' && tokEnd == (int_t)sourceBuffer.length()) {
                return reachedEOS();
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