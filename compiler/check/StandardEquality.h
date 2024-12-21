#pragma once

#include <check/EqualityInfo.h>
#include <check/EqualityTheory.h>
#include <check/Reason.h>
#include <check/SatSolver.h>

namespace check {

struct StandardEquality : EqualityTheory, ReasonTheory {
    using Link = EqualityTheory::Link;
    using EqualityInfo = EquatableValueTheory::EqualityInfo;
    enum class ReasonKind;

    StandardEquality(Solver&);

    void propagateFalseAssignment(Solver&, BooleanValue) override;
    void reapplyFalseAssignment(Solver&, BooleanValue) override;
    void unapplyFalseAssignment(Solver&, BooleanValue) override;

    void checkInvariances(Solver& solver);

    int_t reasonEqId(const Reason&) const;
    int_t reasonDiseqId(const Reason&) const;
    bool isEqualityReason(const Reason&) const;
    std::pair<Value, Value> reasonDiseqOriented(const Reason& reason) const;
    Reason equalityReason(int_t eqId) const;
    Reason disequalityReason(ReasonKind kind, int_t eqId, int_t diseqId) const;

    bool testReason(Solver&, const Reason&) override;
    ClauseAndIndex reasonToClause(Solver&, const Reason&) override;
    void newDecisionLevel(Solver&) override;
    void backtrack(Solver&) override;

    static void forEachParentOf(Solver&, Value value, auto&& callback);
    static void forEachEqualValue(Solver&, Value value, auto&& callback);

    void path(Solver&, Value a, Value b, std::vector<BooleanValue>&);

protected:
    void onNewVariable(Solver&, int_t eqId) override;

    virtual void watch(Solver&, Value, Value) { }
    virtual bool isDisequalityWatched(Solver&, Value, Value) { return false; }

private:
    static void addEdge(Solver&, Value value, Value otherValue, int_t eqId);

    void assignEqual(Solver&, int_t eqId);
    void assignDisequal(Solver&, int_t eqId, int_t diseqId);

    void pathInTree(Solver&, Value a, Value b, std::vector<BooleanValue>&);

    void applyEqual(Solver& solver, int_t eqId, bool propagate);
    void applyDisequal(Solver& solver, int_t eqId, bool propagate);

    static bool connected(Solver& solver, Value a, Value b) {
        return infoFor(solver, a).root == infoFor(solver, b).root;
    }

    static EqualityInfo& infoFor(Solver& solver, Value v) {
        return static_cast<EquatableValueTheory&>(solver.theoryFor(v)).equalityInfo(solver, v);
    }

    struct EqualityTraceEntry {
        Link link;
        Link roots; //!< The root of the source and target respectively
    };

    struct DisequalityTraceEntry {
        uint32_t diseqId;
    };

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

}