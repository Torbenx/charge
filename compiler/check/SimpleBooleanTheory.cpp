#include <check/SimpleBooleanTheory.h>

#include <check/SatSolver.h>

namespace check {

bool SimpleBooleanTheory::assignedPositive(Solver& solver, int_t varId) {
    return solver.assignedFalse(negativeLiteral(varId));
}
bool SimpleBooleanTheory::assignedNegative(Solver& solver, int_t varId) {
    return solver.assignedFalse(positiveLiteral(varId));
}

int_t SimpleBooleanTheory::variableCount(Solver& solver) { return solver.valueCount(*this) / 2; }

std::optional<int_t> SimpleBooleanTheory::findUnassignedVariable(Solver& solver) {
    for (int_t i = find; i < variableCount(solver); i++) {
        if (solver.infoFor(positiveLiteral(i)).tentativelyTrue() || solver.infoFor(negativeLiteral(i)).tentativelyTrue())
            continue;
        find = i;
        return i;
    }
    for (int_t i = 0; i < find; i++) {
        if (solver.infoFor(positiveLiteral(i)).tentativelyTrue() || solver.infoFor(negativeLiteral(i)).tentativelyTrue())
            continue;
        find = i;
        return i;
    }
    return std::nullopt;
}

}