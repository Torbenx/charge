#pragma once

#include <check/Reason.h>

namespace check {

struct ImplicationReasons : ReasonTheory {
    ImplicationReasons(Solver& solver, bool propagating = false)
        : ReasonTheory(solver, propagating) { }

    static BooleanValue reasonNegatedPremise(const Reason& reason) {
        return std::bit_cast<BooleanValue>(reason.data1);
    }
    static BooleanValue reasonConsequence(const Reason& reason) {
        return std::bit_cast<BooleanValue>(reason.data2);
    }

    Reason makeImplicationReason(BooleanValue negatedPremise, BooleanValue consequence) {
        return { (uint32_t)theoryId(), 0, std::bit_cast<uint32_t>(negatedPremise), std::bit_cast<uint32_t>(consequence) };
    }

    bool testReason(Solver& solver, const Reason& reason) override;
    ClauseAndIndex reasonToClause(Solver& solver, const Reason& reason) override;

    void newDecisionLevel(Solver&) override {}
    void backtrack(Solver&) override {}

};

}