#include <check/PartialOrdering.h>

#include <gtest/gtest.h>

namespace check {

// ---------------------------- Unordered ---------------------------

std::string PartialOrderingTheory::Unordered::formatPositiveLiteral(Solver& solver, int_t varId) {
    auto [a, b] = theory()->formatValues(solver, m_handles[varId]);
    return "(" + a + " >< " + b + ")";
}

std::string PartialOrderingTheory::Unordered::formatNegativeLiteral(Solver& solver, int_t varId) {
    return "!" + formatPositiveLiteral(solver, varId);
}

void PartialOrderingTheory::Unordered::propagateAssignment(Solver& solver, BooleanValue literal) {
    theory()->propagateAssignment(solver, m_handles[variableId(literal)], std::partial_ordering::unordered, isPositive(literal));
}

void PartialOrderingTheory::Unordered::unapplyAssignment(Solver& solver, BooleanValue literal) {
    theory()->unapplyAssignment(solver, m_handles[variableId(literal)], std::partial_ordering::unordered, isPositive(literal));
}

void PartialOrderingTheory::Unordered::reapplyAssignment(Solver& solver, BooleanValue literal) {
    theory()->reapplyAssignment(solver, m_handles[variableId(literal)], std::partial_ordering::unordered, isPositive(literal));
}

uint32_t PartialOrderingTheory::Unordered::labelOfVariable(Solver&, int_t varId) {
    VERIFY_NOT_REACHED();
}

bool PartialOrderingTheory::Unordered::isVariableActive(Solver& solver, int_t varId) {
    return theory()->isActive(solver, m_handles[varId]);
}

void PartialOrderingTheory::Unordered::collectVariableInactiveReasons(Solver& solver, int_t varId, std::vector<BooleanValue>& clause) {
    theory()->collectInactiveReasons(solver, m_handles[varId], clause);
}

// ---------------------------- Equality ----------------------------

bool PartialOrderingTheory::Equality::isUnitDisequal(Solver& solver, Value a, Value b) {
    return !theory()->possibleOrderings(solver, a, b).test(std::partial_ordering::equivalent);
}
OrientedPair PartialOrderingTheory::Equality::equalityLink(int_t eqId) {
    return theory()->at(m_handles[eqId]).link;
}

int_t PartialOrderingTheory::Equality::lookupEqualityVariable(Solver& solver, Value a, Value b) {
    return variableId(theory()->literal(theory()->order(solver, a, b), std::partial_ordering::equivalent));
}

uint32_t PartialOrderingTheory::Equality::labelOfVariable(Solver&, int_t varId) {
    return theory()->labelAt(m_handles[varId]);
}

// ---------------------- PartialOrderingTheory ---------------------

PartialOrderingTheory::PartialOrderingTheory(Solver& solver, uint64_t baseLabel)
    : m_equality(solver, baseLabel + 100)
    , m_unordered(solver, baseLabel + 200) {
}

}