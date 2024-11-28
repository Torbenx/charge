#pragma once

#include <check/ValueTheory.h>

namespace check {

struct EquatableValueTheory::EqualityInfo {
    struct TreeNode {
        Value value;
        uint32_t subTreeSize = 1;
        //! Node where this tree attaches to its parent tree
        /*!
        Always lies in the parent tree before this node. Stored is the offset in the parent tree.
        */
        uint32_t linkSource = 0;
        //! Node where the parent tree attaches to this tree
        /*!
        Always lies with in this subtree (linkTarget < subTreeSize). Stored is the offset from this node.
        */
        uint32_t linkTarget = 0;

        bool operator==(const TreeNode&) const = default;
    };

    //! Used to detect when two values become equal
    /*!
    There are two (active) edges per equality literal defined by this theory.
    When \p otherValue belongs to the same tree as the edge then the equality given by \p eqId is implied.
    */
    struct Edge {
        Value otherValue;
        uint32_t eqId;

        bool operator==(const Edge&) const = default;
    };

    explicit EqualityInfo(Value v)
        : root(v) { }

    Value root;
    int32_t treeOffset = -1; //!< Offset in root's tree
    uint32_t edgesOffset = 0; //!< Offset of this values edges in root's edges
    uint32_t backtrackCounter = 0;
    uint32_t backtrackTracePosition = -1; //!< Position of the trace entry where this value ceased to be a root or -1 if it is a root
    std::vector<TreeNode> tree;
    std::vector<Edge> edges;
    std::vector<uint32_t> disequalities; //!< Sorted list of disequalities this node is a part of
};

}