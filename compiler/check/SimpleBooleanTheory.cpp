#include <check/SimpleBooleanTheory.h>

#include <check/SatSolver.h>

namespace check {

bool SimpleBooleanTheory::assignedPositive(Solver& solver, int_t varId) {
    return solver.assignedFalse(negativeLiteral(varId));
}
bool SimpleBooleanTheory::assignedNegative(Solver& solver, int_t varId) {
    return solver.assignedFalse(positiveLiteral(varId));
}

}