#include <check/EqualityTheory.h>

#include <check/SatSolver.h>

namespace check {

EqualityTheory::Link EqualityTheory::orient(Solver& solver, Value a, Value b) {
    if (solver.compare(a, b) > 0)
        std::swap(a, b);
    return Link { a, b };
}

std::strong_ordering EqualityTheory::compare(Solver& solver, Link a, Link b) {
    auto targetCmp = solver.compare(a.target, b.target);
    if (targetCmp != 0)
        return targetCmp;
    return solver.compare(a.source, b.source);
}

int_t EqualityTheory::equalityVariable(Solver& solver, Value a, Value b) {
    Link l = orient(solver, a, b);
    auto cmp = [&solver](Link a, Link b) { return compare(solver, a, b); };
    int_t varId = equalities.get(l, cmp);
    if (varId == variableCount()) {
        newVariable();
        onNewVariable(solver, varId);
    }

    VERIFY(varId < variableCount());
    return varId;
}

std::string EqualityTheory::formatPositiveLiteral(Solver& solver, int_t varId) {
    auto eq = equalities.at(varId);
    return fmt::format("({} == {})", solver.formatValue(eq.source), solver.formatValue(eq.target));
}

std::string EqualityTheory::formatNegativeLiteral(Solver& solver, int_t varId) {
    auto eq = equalities.at(varId);
    return fmt::format("({} != {})", solver.formatValue(eq.source), solver.formatValue(eq.target));
}

}