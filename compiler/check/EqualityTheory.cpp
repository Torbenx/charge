#include <check/EqualityTheory.h>

#include <check/SatSolver.h>

namespace check {

uint32_t EqualityTheory::LinkSet::makeNode(Solver&, const Link& link, TreeLabel label) {
    return Base::makeNode(label, link);
}
std::strong_ordering EqualityTheory::LinkSet::compare(Solver& solver, const Link& a, const Link& b) {
    auto targetCmp = solver.compare(a.target, b.target);
    if (targetCmp != 0)
        return targetCmp;
    return solver.compare(a.source, b.source);
}

EqualityTheory::Link EqualityTheory::orient(Solver& solver, Value a, Value b) {
    if (solver.compare(a, b) > 0)
        std::swap(a, b);
    return Link { a, b };
}

int_t EqualityTheory::equalityVariable(Solver& solver, Value a, Value b) {
    VERIFY(a != b);
    Link l = orient(solver, a, b);
    int_t varId = equalities.get(solver, l);
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

void EqualityTheory::collectVariableInactiveReasons(Solver& solver, int_t varId, std::vector<BooleanValue>& clause) {
    auto [source, target] = equalities.at(varId);
    solver.collectInactiveReasons(source, clause);
    solver.collectInactiveReasons(target, clause);
}

bool EqualityTheory::isVariableActive(Solver& solver, int_t varId) {
    auto [source, target] = equalities.at(varId);
    return solver.isActive(source) && solver.isActive(target);
}


}