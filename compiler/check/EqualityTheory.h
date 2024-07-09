#pragma once

#include <check/LiteralInfo.h>
#include <check/ValueTheory.h>

namespace check {

struct TreeLabel {
    //! Returns the label for the root node
    static TreeLabel rootLabel() { return TreeLabel(std::numeric_limits<uint32_t>::max() >> 1); }

    //! Returns the label for left or right child of this node
    [[nodiscard]] TreeLabel extend(bool right) const {
        uint32_t tmp1 = m_label + 1;
        uint32_t exLabel = tmp1 & (right ? (uint32_t)-1 : m_label);
        exLabel |= (tmp1 ^ m_label) >> 2;
        return TreeLabel(exLabel);
    }

    //! Returns the depth of this node
    /*!
    The root node is defined to have depth 0.
    */
    int_t depth() const { return 31 - std::countr_one(m_label); }

    uint32_t label() const { return m_label; }

private:
    TreeLabel(uint32_t label)
        : m_label(label) { }

    uint32_t m_label;
};

template<typename T>
struct FlatTreeSet {
    static constexpr uint32_t NIL_INDEX = -1;

    struct Node {
        Node(const T& data, TreeLabel label)
            : label(label), data(data) { }

        uint32_t leftChild = NIL_INDEX;
        uint32_t rightChild = NIL_INDEX;
        TreeLabel label;
        T data;
    };

    template<typename Comp>
    int_t get(const T& data, Comp comp) {
        if (rootNodeIndex == NIL_INDEX) {
            rootNodeIndex = nodes.size();
            nodes.emplace_back(data, TreeLabel::rootLabel());
            return rootNodeIndex;
        }
        uint32_t nodeIndex = rootNodeIndex;
        for (;;) {
            Node& node = nodes[nodeIndex];
            auto c = comp(data, node.data);
            if (c == 0)
                return nodeIndex;
            uint32_t& nextIndex = c < 0 ? node.leftChild : node.rightChild;
            if (nextIndex != NIL_INDEX) {
                nodeIndex = nextIndex;
                continue;
            }

            nextIndex = nodes.size();
            int_t result = nodes.size();
            nodes.emplace_back(data, node.label.extend(c > 0));
            return result;
        }
    }

    T& at(int_t index) { return nodes[index].data; }
    const T& at(int_t index) const { return nodes[index].data; }

    uint32_t label(int_t index) const { return nodes[index].label.label(); }

    int_t nodeCount() const { return nodes.size(); }

    std::vector<Node> nodes;
    uint32_t rootNodeIndex = NIL_INDEX;
    uint32_t maxDepth = 1;
};

struct EqualityTheory : SimpleBooleanTheory<> {
    using SimpleBooleanTheory<>::SimpleBooleanTheory;

    //! Returns a value that is true if and only if \p a == \p b
    BooleanValue equality(Solver& solver, Value a, Value b) {
        return positiveLiteral(equalityVariable(solver, a, b));
    }

    //! Returns a value that is true if and only if \p a != \p b
    BooleanValue disequality(Solver& solver, Value a, Value b) {
        return negativeLiteral(equalityVariable(solver, a, b));
    }

    std::string formatPositiveLiteral(Solver&, int_t varId) override;
    std::string formatNegativeLiteral(Solver&, int_t varId) override;

    uint64_t labelOf(Solver&, Value v) override {
        BooleanValue lit { v };
        return baseLabel + (uint64_t)equalities.label(variableId(lit)) * 2 + isPositive(lit);
    }

protected:
    struct Link {
        Value source;
        Value target;

        bool operator==(const Link&) const = default;
    };

    int_t equalityVariable(Solver&, Value a, Value b);

    static Link orient(Solver&, Value a, Value b);

    static std::strong_ordering compare(Solver&, Link, Link);

    virtual void onNewVariable(Solver&, int_t eqId) = 0;

    FlatTreeSet<Link> equalities;
    uint64_t baseLabel = 0;
};

}