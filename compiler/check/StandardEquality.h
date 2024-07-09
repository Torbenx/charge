#pragma once

#include <check/EqualityTheory.h>
#include <check/Reason.h>
#include <check/EqualityInfo.h>
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

private:
    void forEachParentOf(Solver&, Value value, auto&& callback);
    void addEdge(Solver&, Value value, Value otherValue, int_t eqId);
    void onNewVariable(Solver&, int_t eqId) override;

    void assignEqual(Solver&, int_t eqId);
    void assignDisequal(Solver&, int_t eqId, int_t diseqId);

    void path(Solver&, Value a, Value b, std::vector<BooleanValue>&);
    void pathInTree(Solver&, Value a, Value b, std::vector<BooleanValue>&);

    void applyEqual(Solver& solver, int_t eqId, bool propagate);
    void applyDisequal(Solver& solver, int_t eqId, bool propagate);

    bool connected(Solver& solver, Value a, Value b) {
        return infoFor(solver, a).root == infoFor(solver, b).root;
    }

    EqualityInfo& infoFor(Solver& solver, Value v) {
        return static_cast<EquatableValueTheory&>(solver.theoryFor(v)).equalityInfo(solver, v);
    }

    struct TraceEntry {
        Link link;
        Link roots; //!< The root of the source and target respectively
    };

    std::vector<TraceEntry> equalityTrace;
    std::vector<uint32_t> disequalityTrace;
    std::vector<uint32_t> equalityDecisionPoints; //!< Trace sizes at the respective decision levels
    std::vector<uint32_t> disequalityDecisionPoints; //!< Trace sizes at the respective decision levels

    //! Copy of the trace at the time of backtracking. Used to reconstruct paths after backtracking
    std::vector<TraceEntry> backtrackTrace;

    uint32_t backtrackCounter = 0;
};

}