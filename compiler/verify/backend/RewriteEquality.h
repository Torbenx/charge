#pragma once

#include <verify/backend/Solver.h>

namespace verify::backend {

struct RewriteEquality {
    RewriteEquality(Solver&, ValueKind, TheoryId);

    BooleanValue makeEquality(PairHandle pair) const {
        return { m_theory, pair.pairId() * 2 };
    }
    PairHandle pairOf(BooleanValue equality) const {
        return { m_valueKind, equality.id() / 2 };
    }

    void applyEqual(Solver& solver, PairHandle eqPair, bool propagate);
    void applyDisequal(Solver& solver, PairHandle diseqPair, bool propagate);

    void checkInvariances(Solver&);

    bool testReason(Solver&, BooleanValue, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&);
    void newDecisionLevel(Solver&);
    void backtrack(Solver&);

    void newPair(Solver&, PairHandle);

    void forEachParentOf(Value value, auto&& callback);
    void forEachEqualValue(Value value, auto&& callback);

    Value rewrite(Value v) { return infoFor(v).root; }

private:
    struct EqualityInfo {
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
        When \p otherValue belongs to the same tree as the edge then the equality given by \p pair is implied.
        */
        struct Edge {
            Value otherValue;
            PairHandle pair;

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

    bool connected(Value a, Value b) {
        return infoFor(a).root == infoFor(b).root;
    }

    void addEdge(Value value, Value otherValue, PairHandle pair);

    void assignEqual(Solver&, PairHandle assignPair);
    void assignDisequal(Solver&, PairHandle assignPair, PairHandle diseqPair);
    void assignDisequalByAlwaysDisequal(Solver&, PairHandle assignPair, Value alwaysDiseqA, Value alwaysDiseqB);

    void pathInTree(Solver&, Value a, Value b, ClauseBuilder&);
    void path(Solver&, Value a, Value b, ClauseBuilder&);

    EqualityInfo& infoFor(Value v) {
        return equalityInfos[v];
    }

    struct EqualityTraceEntry {
        Pair link;
        Pair roots; //!< The root of the source and target respectively
    };

    struct DisequalityTraceEntry {
        PairHandle diseqPair;
    };

    KindData<EqualityInfo> equalityInfos;

    std::vector<EqualityTraceEntry> equalityTrace;
    std::vector<DisequalityTraceEntry> disequalityTrace;
    std::vector<uint32_t> equalityDecisionPoints; //!< Trace sizes at the respective decision levels
    std::vector<uint32_t> disequalityDecisionPoints; //!< Trace sizes at the respective decision levels

    //! Copy of the trace at the time of backtracking. Used to reconstruct paths after backtracking
    std::vector<EqualityTraceEntry> backtrackTrace;

    uint32_t backtrackCounter = 0;
    TheoryId m_theory;
    ValueKind m_valueKind;
};

inline void RewriteEquality::forEachParentOf(Value value, auto&& callback) {
    const auto& valueInfo = infoFor(value);
    callback(valueInfo.root);

    int_t valueIndex = valueInfo.treeOffset;
    const auto& tree = infoFor(valueInfo.root).tree;
    int_t rootIndex = -1;
    for (;;) {
        if (rootIndex == valueIndex)
            break;

        int_t index = rootIndex + 1;
        for (;;) {
            int_t nextIndex = index + tree[index].subTreeSize;
            if (nextIndex > valueIndex)
                break;
            index = nextIndex;
        }
        rootIndex = index;

        callback(tree[rootIndex].value);
    }
}

inline void RewriteEquality::forEachEqualValue(Value value, auto&& callback) {
    const auto& tree = infoFor(infoFor(value).root).tree;
    for (const auto& node : tree) {
        callback(node.value);
    }
}

}