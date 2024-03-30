#pragma once

#include <parse/parse_gen.h>

#include <utility>
#include <vector>

namespace parse {

struct TokenHandle {
    uint32_t offset;
};

enum class TokenKind : uint8_t {
#define TOKEN(kind, type, prec) kind,

#include <parse/tokens.inc>

FirstUnaryExpr = LogicalNotExpr,
LastUnaryExpr = DereferenceExpr,
};
std::string_view nameString(TokenKind);
inline bool isUnaryExpr(TokenKind kind) {
    return kind >= TokenKind::FirstUnaryExpr && kind <= TokenKind::LastUnaryExpr;
}

#define ENUMERATE_SCOPE_KINDS \
    SCOPE(Invalid)            \
    SCOPE(IfExpr)             \
    SCOPE(IfExprOrStmt)       \
    SCOPE(CompoundStmt)       \
    SCOPE(Paren)              \
    SCOPE(Square)             \
    SCOPE(Brace)              \
    SCOPE(LeftExpr)           \
    SCOPE(RightExpr)          \
    SCOPE(VariableType)       \
    SCOPE(IfBranch)           \
    SCOPE(ElseBranch)         \
    SCOPE(PlainStatement)     \
    SCOPE(Argument)           \
    SCOPE(Parameter)          \
    SCOPE(Namespace)          \
    SCOPE(FunctionBody)       \
    SCOPE(ReturnType)         \
    SCOPE(FunctionParameters) \
    SCOPE(Type)               \
    SCOPE(HasTypeExpr)        \
    SCOPE(TemplateParameters)

enum class ScopeKind : uint8_t {
#define SCOPE(kind) kind,
    ENUMERATE_SCOPE_KINDS
#undef SCOPE
};
std::string_view nameString(ScopeKind);

struct TokenInfo : TaggedSourceLocation<TokenKind> {
    uint32_t dataBits = 0;

    TokenInfo(TokenKind kind, SourceLocation location, uint32_t data = 0)
        : TaggedSourceLocation<TokenKind>(kind, location), dataBits(data) { }

    TokenKind kind() const { return tag(); }
    uint32_t data() const { return dataBits; }

    void setKind(TokenKind kind) {
        setTag(kind);
    }
};

struct LineInfo {
    const char* begin = nullptr;
};

enum class WhitespaceKind : uint8_t {
    LineComment,
    BlockComment,
    EOS,
};

struct WhitespaceInfo : TaggedSourceLocation<WhitespaceKind> {
    uint32_t length = 0;
};

struct Output {
    std::vector<TokenInfo> tokens;
    std::vector<LineInfo> lines;
    std::vector<WhitespaceInfo> whitespace;
    std::string_view source;
    Output(std::string_view source)
        : source(source) {
        lines.push_back({ source.data() });
    }

    const char* sourcePointer(SourceLocation loc) const {
        return lines[loc.lineIndex()].begin + loc.offsetInLine();
    }

    std::string_view whitespaceSpelling(WhitespaceInfo info) const {
        return std::string_view(sourcePointer(info.location()), info.length);
    }

    TokenHandle currentToken() const {
        return { (uint32_t)tokens.size() };
    }
};

template<typename Impl>
struct OutputVisitor {
    Impl* impl() { return static_cast<Impl*>(this); }

    void visit(const Output& output) {
        auto endLoc = SourceLocation(0, output.lines.size());
        auto tokenIt = output.tokens.begin();
        auto whitespaceIt = output.whitespace.begin();
        for (;;) {
            auto result = *tokenIt <=> *whitespaceIt;
            if (result < 0) {
                impl()->visitToken(*tokenIt);
                tokenIt += 1;
            } else if (result > 0) {
                impl()->visitWhitespace(*whitespaceIt);
                whitespaceIt += 1;
            } else {
                VERIFY(tokenIt + 1 == output.tokens.end() && whitespaceIt + 1 == output.whitespace.end());
                break;
            }
        }
    }
};

}