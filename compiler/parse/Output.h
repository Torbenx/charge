#pragma once

#include <parse/parse_gen.h>

#include <utility>
#include <vector>

namespace parse {

struct NodeHandle {
    uint32_t offset;
};

enum class NodeKind : uint8_t {
#define NODE(kind, type, prec) kind,

#include "nodes.inc"
};
std::string_view nameString(NodeKind);

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

struct SourceLocation {
    SourceLocation(uint32_t offsetInLine, uint32_t lineIndex)
        : m_offsetInLine(offsetInLine), m_lineIndex(lineIndex) { }

    uint32_t lineIndex() const { return m_lineIndex; }
    uint32_t lineNumber() const { return m_lineIndex + 1; }
    uint32_t offsetInLine() const { return m_offsetInLine; }
    uint32_t column() const { return m_offsetInLine + 1; }

private:
    [[maybe_unused]] uint32_t tagBits : 8 = 0;
    uint32_t m_offsetInLine : 24 = 0;
    uint32_t m_lineIndex = 0;
};
template<typename T>
struct TaggedSourceLocation {
    TaggedSourceLocation(T tag, SourceLocation loc)
        : tagBits(std::bit_cast<uint8_t>(tag))
        , m_offsetInLine(loc.offsetInLine())
        , m_lineIndex(loc.lineIndex()) { }

    SourceLocation location() const {
        return std::bit_cast<SourceLocation>(*this);
    }
    T tag() const { return std::bit_cast<T>(static_cast<uint8_t>(tagBits)); }
    void setLocation(SourceLocation loc) {
        this->m_offsetInLine = loc.offsetInLine();
        this->m_lineIndex = loc.lineIndex();
    }
    void setTag(T tag) {
        tagBits = std::bit_cast<uint8_t>(tag);
    }
    uint32_t lineIndex() const { return m_lineIndex; }
    uint32_t lineNumber() const { return m_lineIndex + 1; }
    uint32_t offsetInLine() const { return m_offsetInLine; }
    uint32_t column() const { return m_offsetInLine + 1; }

private:
    uint32_t tagBits : 8 = 0;
    uint32_t m_offsetInLine : 24 = 0;
    uint32_t m_lineIndex = 0;
};

template<typename T1, typename T2>
auto operator<=>(TaggedSourceLocation<T1> left, TaggedSourceLocation<T2> right) {
    return (std::bit_cast<uint64_t>(left) >> 8) <=> (std::bit_cast<uint64_t>(right) >> 8);
}

inline auto operator<=>(SourceLocation left, SourceLocation right) {
    return (std::bit_cast<uint64_t>(left) >> 8) <=> (std::bit_cast<uint64_t>(right) >> 8);
}

struct Node : TaggedSourceLocation<NodeKind> {
    uint32_t dataBits = 0;

    Node(NodeKind kind, SourceLocation location, uint32_t data = 0)
        : TaggedSourceLocation<NodeKind>(kind, location), dataBits(data) { }

    NodeKind kind() const { return tag(); }
    uint32_t data() const { return dataBits; }

    void setKind(NodeKind kind) {
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
    std::vector<Node> nodes;
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

    NodeHandle currentNode() const {
        return { (uint32_t)nodes.size() };
    }
};

template<typename Impl>
struct OutputVisitor {
    Impl* impl() { return static_cast<Impl*>(this); }

    void visit(const Output& output) {
        auto endLoc = SourceLocation(0, output.lines.size());
        auto nodeIt = output.nodes.begin();
        auto whitespaceIt = output.whitespace.begin();
        for (;;) {
            auto result = *nodeIt <=> *whitespaceIt;
            if (result < 0) {
                impl()->visitNode(*nodeIt);
                nodeIt += 1;
            } else if (result > 0) {
                impl()->visitWhitespace(*whitespaceIt);
                whitespaceIt += 1;
            } else {
                VERIFY(nodeIt + 1 == output.nodes.end() && whitespaceIt + 1 == output.whitespace.end());
                break;
            }
        }
    }
};

}