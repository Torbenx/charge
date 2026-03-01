#pragma once

#include <check/Reason.h>
#include <check/SatSolver.h>

namespace check {

struct StandardEquality : EqualityTheory, ReasonTheory {
    StandardEquality(Solver&, ValueKind);

    void propagateAssignment(Solver&, BooleanValue) override;
    void reapplyAssignment(Solver&, BooleanValue) override;
    void unapplyAssignment(Solver&, BooleanValue) override;

    void checkInvariances(Solver& solver);

    bool isUnitDisequalityReason(const Reason&) const;
    int_t reasonDiseqId(const Reason&) const;
    std::pair<Value, Value> reasonDiseqOriented(const Reason& reason);
    Reason equalityReason() const;
    Reason disequalityReason(bool swappedConnectivity, int_t diseqId) const;
    Reason unitDisequalityReason(Value diseqA, Value diseqB);

    bool testReason(Solver&, BooleanValue, const Reason&) override;
    ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&) override;
    void newDecisionLevel(Solver&) override;
    void backtrack(Solver&) override;

    void forEachParentOf(Solver&, Value value, auto&& callback);
    void forEachEqualValue(Solver&, Value value, auto&& callback);

    void path(Solver&, Value a, Value b, std::vector<BooleanValue>&);

    BooleanValue newBoolean(Solver&) override;

protected:
    /*! \brief Return whether \p a and \p b are always disequal

    If \p a and \p b are always disequal any values less the either \p a or \p b must also be always
    disequal to the other one (and this function must be able to detect this).
    */
    virtual bool isUnitDisequal(Solver&, [[maybe_unused]] Value a, [[maybe_unused]] Value b) { return false; }

    //! Must return the variable id for equality of the two values. The variable is guaranteed to exist when this is called.
    virtual int_t lookupEqualityVariable(Solver&, Value, Value) = 0;

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

    void addEdge(Solver&, Value value, Value otherValue, int_t eqId);

    void assignEqual(Solver&, int_t eqId);
    void assignDisequal(Solver&, int_t eqId, int_t diseqId);
    void unitAssignDisequal(Solver&, int_t eqId, Value unitDiseqA, Value unitDiseqB);

    void pathInTree(Solver&, Value a, Value b, std::vector<BooleanValue>&);

    void applyEqual(Solver& solver, int_t eqId, bool propagate);
    void applyDisequal(Solver& solver, int_t eqId, bool propagate);

    bool connected(Solver& solver, Value a, Value b) {
        return infoFor(solver, a).root == infoFor(solver, b).root;
    }

    EqualityInfo& infoFor(Solver&, Value v) {
        // TODO: Solver& parameter can be removed
        return equalityInfos[v];
    }

    struct EqualityTraceEntry {
        Link link;
        Link roots; //!< The root of the source and target respectively
    };

    struct DisequalityTraceEntry {
        uint32_t diseqId;
    };

    KindData<EqualityInfo> equalityInfos;

    std::vector<EqualityTraceEntry> equalityTrace;
    std::vector<DisequalityTraceEntry> disequalityTrace;
    std::vector<uint32_t> equalityDecisionPoints; //!< Trace sizes at the respective decision levels
    std::vector<uint32_t> disequalityDecisionPoints; //!< Trace sizes at the respective decision levels

    //! Copy of the trace at the time of backtracking. Used to reconstruct paths after backtracking
    std::vector<EqualityTraceEntry> backtrackTrace;

    uint32_t backtrackCounter = 0;
};

inline void StandardEquality::forEachParentOf(Solver& solver, Value value, auto&& callback) {
    const auto& valueInfo = infoFor(solver, value);
    callback(valueInfo.root);

    int_t valueIndex = valueInfo.treeOffset;
    const auto& tree = infoFor(solver, valueInfo.root).tree;
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

inline void StandardEquality::forEachEqualValue(Solver& solver, Value value, auto&& callback) {
    const auto& tree = infoFor(solver, infoFor(solver, value).root).tree;
    for (const auto& node : tree) {
        callback(node.value);
    }
}

struct BasicEquality : StandardEquality {
    using StandardEquality::StandardEquality;

    BooleanValue equality(Solver& solver, Value a, Value b) {
        if (a == b)
            return builtins::true_literal;

        return positiveLiteral(equalityVariable(solver, a, b));
    }

    BooleanValue disequality(Solver& solver, Value a, Value b) {
        if (a == b)
            return builtins::false_literal;

        return negativeLiteral(equalityVariable(solver, a, b));
    }

    int_t equalityVariable(Solver& solver, Value a, Value b) {
        int_t varId = m_equalities.get(solver, a, b);
        if (varId == variableCount(solver))
            VERIFY(varId == newVariable(solver));
        return varId;
    }

    Link equalityLink(int_t varId) override { return m_equalities.at(varId); }
    int_t lookupEqualityVariable(Solver& solver, Value a, Value b) override { return m_equalities.get(solver, a, b); }
    uint32_t labelOfVariable(Solver&, int_t varId) override { return m_equalities.label(varId); }

    SymmetricBinaryRelation<> m_equalities;
};

}