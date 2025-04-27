#include <check/EqualityTheory.h>

#include <check/SatSolver.h>

namespace check {

std::string EqualityTheory::formatPositiveLiteral(Solver& solver, int_t varId) {
    auto eq = equalityLink(varId);
    return fmt::format("({} == {})", solver.formatValue(eq.source), solver.formatValue(eq.target));
}

std::string EqualityTheory::formatNegativeLiteral(Solver& solver, int_t varId) {
    auto eq = equalityLink(varId);
    return fmt::format("({} != {})", solver.formatValue(eq.source), solver.formatValue(eq.target));
}

void EqualityTheory::collectVariableInactiveReasons(Solver& solver, int_t varId, std::vector<BooleanValue>& clause) {
    auto [source, target] = equalityLink(varId);
    solver.collectInactiveReasons(source, clause);
    solver.collectInactiveReasons(target, clause);
}

bool EqualityTheory::isVariableActive(Solver& solver, int_t varId) {
    auto [source, target] = equalityLink(varId);
    return solver.isActive(source) && solver.isActive(target);
}

}