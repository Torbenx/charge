#include <check/ImplicationReasons.h>

#include <check/SatSolver.h>

namespace check {

bool ImplicationReasons::testReason(Solver& solver, const Reason& reason) {
    return solver.assignedFalse(reasonNegatedPremise(reason));
}

ReasonTheory::ClauseAndIndex ImplicationReasons::reasonToClause(Solver& solver, const Reason& reason) {
    auto& clause = solver.scratchClause();
    clause.push_back(reasonNegatedPremise(reason));
    clause.push_back(reasonConsequence(reason));
    return { clause, 1 };
}

}