#pragma once

#include <sema/Value.h>

namespace sema {

#define ENUMERATE_NODE_KINDS        \
    KIND(ReferenceExpr, Reference)  \
    KIND(ConstantExpr, Pure)        \
    KIND(CallExpr, Owning)          \
    KIND(LetDecl, Statement)        \
    KIND(ExpressionStmt, Statement) \
    KIND(Function, Statement)

enum class NodeKind : uint8_t {
#define KIND(kind, cat) kind,

    ENUMERATE_NODE_KINDS

#undef KIND
};
std::string_view nameString(NodeKind kind);

enum class NodeCategory {
    Pure,
    Reference,
    Owning,
    Statement,
};
inline NodeCategory nodeCategory(NodeKind kind) {
    switch (kind) {

#define KIND(kind, cat)  \
    case NodeKind::kind: \
        return NodeCategory::cat;

        ENUMERATE_NODE_KINDS

#undef KIND
    }
    VERIFY_NOT_REACHED();
}

inline bool isExpression(NodeKind kind) { return nodeCategory(kind) != NodeCategory::Statement; }

struct ChildrenRange;
struct ChildrenIterator;

union ExprData {
    Value constant;
    Value callBase; // either a program or a parameterize constant
};
union NodeData {
    struct {
        Type type;
        ExprData u;
    } expr;
    struct {
        Type type;
    } decl;
    struct {
    } empty;
};

struct Node {
    Node(NodeKind kind, SourceLocation location, int_t childrenCount, int_t subTreeSize, NodeData data)
        : m_location(kind, location), m_childrenCount(childrenCount), m_subTreeSize(subTreeSize), u(data) { }

    int_t subTreeSize() const { return m_subTreeSize; }
    int_t childrenCount() const { return m_childrenCount; }
    ChildrenRange reverseChildren();

    void validateTreeProperty();

    NodeKind kind() const { return m_location.tag(); }
    SourceLocation location() const { return m_location.location(); }

    TaggedSourceLocation<NodeKind> m_location;
    uint32_t m_childrenCount;
    uint32_t m_subTreeSize; // Size of the sub-tree of this node including this node itself.
    NodeData u;
};
static_assert(sizeof(Node) == 24);

// iterates children in reverse order
struct ChildrenIterator {
    using value_type = Node*;
    using difference_type = int_t;

    ChildrenIterator() = default;
    explicit ChildrenIterator(Node* node)
        : m_node(node) { }
    ChildrenIterator(const ChildrenIterator&) = default;
    ChildrenIterator(ChildrenIterator&&) = default;
    ChildrenIterator& operator=(const ChildrenIterator&) = default;
    ChildrenIterator& operator=(ChildrenIterator&&) = default;

    ChildrenIterator& operator++() {
        advance();
        return *this;
    }
    ChildrenIterator operator++(int) {
        ChildrenIterator copy = *this;
        advance();
        return copy;
    }
    Node* operator*() const { return m_node; }
    Node* operator->() const { return m_node; }
    operator Node*() const { return m_node; }
    Node* node() const { return m_node; }
    auto operator<=>(const ChildrenIterator& other) const {
        return m_node <=> other.m_node;
    }
    bool operator==(const ChildrenIterator&) const = default;

private:
    void advance() {
        m_node -= m_node->subTreeSize();
    }

    Node* m_node = nullptr;
};
static_assert(std::forward_iterator<ChildrenIterator>);

struct ChildrenRange {
    ChildrenRange() = default;
    ChildrenRange(Node* node)
        : node(node) { }
    ChildrenRange(const ChildrenRange&) = default;
    ChildrenRange(ChildrenRange&&) = default;
    ChildrenRange& operator=(const ChildrenRange&) = default;
    ChildrenRange& operator=(ChildrenRange&&) = default;

    ChildrenIterator begin() const {
        return ChildrenIterator(node - 1);
    }
    ChildrenIterator end() const {
        return std::next(ChildrenIterator(node));
    }
    Node* parent() const { return node; }

private:
    Node* node = nullptr;
};

inline ChildrenRange Node::reverseChildren() {
    return ChildrenRange(this);
}

}