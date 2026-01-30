#include <check/PartialOrdering.h>

namespace check {

// ---------------------------- Unordered ---------------------------

PartialOrderingTheory* PartialOrderingTheory::Unordered::theory() {
    return ReverseMemberPointer<&PartialOrderingTheory::m_unordered>::reverse(this);
}

BooleanValue PartialOrderingTheory::Unordered::newLiteral(Solver& solver, InternalHandle handle) {
    VERIFY((int_t)m_handles.size() == variableCount());
    m_handles.push_back(handle);
    return positiveLiteral(newVariable(solver));
}

std::string PartialOrderingTheory::Unordered::formatPositiveLiteral(Solver& solver, int_t varId) {
    auto [a, b] = theory()->linkAt(m_handles[varId]);
    return "(" + solver.formatValue(a) + " <> " + solver.formatValue(b) + ")";
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
    return theory()->labelAt(m_handles[varId]);
}

bool PartialOrderingTheory::Unordered::isVariableActive(Solver& solver, int_t varId) {
    return theory()->isOrderingActive(solver, m_handles[varId]);
}

void PartialOrderingTheory::Unordered::collectVariableInactiveReasons(Solver& solver, int_t varId, std::vector<BooleanValue>& clause) {
    theory()->collectOrderingInactiveReasons(solver, m_handles[varId], clause);
}

// ---------------------------- Equality ----------------------------

PartialOrderingTheory* PartialOrderingTheory::Equality::theory() {
    return ReverseMemberPointer<&PartialOrderingTheory::m_equality>::reverse(this);
}

BooleanValue PartialOrderingTheory::Equality::newLiteral(Solver& solver, InternalHandle handle) {
    VERIFY((int_t)m_handles.size() == variableCount());
    m_handles.push_back(handle);
    return positiveLiteral(newVariable(solver));
}

bool PartialOrderingTheory::Equality::isUnitDisequal(Solver& solver, Value a, Value b) {
    return !theory()->possibleOrderings(solver, a, b).test(std::partial_ordering::equivalent);
}
OrientedPair PartialOrderingTheory::Equality::equalityLink(int_t eqId) {
    return theory()->linkAt(m_handles[eqId]);
}

int_t PartialOrderingTheory::Equality::lookupEqualityVariable(Solver& solver, Value a, Value b) {
    return variableId(theory()->equality(solver, a, b));
}

uint32_t PartialOrderingTheory::Equality::labelOfVariable(Solver&, int_t varId) {
    return theory()->labelAt(m_handles[varId]);
}

bool PartialOrderingTheory::Equality::isVariableActive(Solver& solver, int_t varId) {
    // Overwrite base implementation so it can be customized
    return theory()->isOrderingActive(solver, m_handles[varId]);
}

void PartialOrderingTheory::Equality::collectVariableInactiveReasons(Solver& solver, int_t varId, std::vector<BooleanValue>& clause) {
    // Overwrite base implementation so it can be customized
    theory()->collectOrderingInactiveReasons(solver, m_handles[varId], clause);
}

void PartialOrderingTheory::Equality::propagateAssignment(Solver& solver, BooleanValue literal) {
    StandardEquality::propagateAssignment(solver, literal);
    theory()->propagateAssignment(solver, m_handles[variableId(literal)], std::partial_ordering::equivalent, isPositive(literal));
}

void PartialOrderingTheory::Equality::unapplyAssignment(Solver& solver, BooleanValue literal) {
    StandardEquality::unapplyAssignment(solver, literal);
    theory()->unapplyAssignment(solver, m_handles[variableId(literal)], std::partial_ordering::equivalent, isPositive(literal));
}

void PartialOrderingTheory::Equality::reapplyAssignment(Solver& solver, BooleanValue literal) {
    StandardEquality::reapplyAssignment(solver, literal);
    theory()->reapplyAssignment(solver, m_handles[variableId(literal)], std::partial_ordering::equivalent, isPositive(literal));
}

// -------------------------- OrderingSets --------------------------

PartialOrderingTheory* PartialOrderingTheory::OrderingSets::theory() {
    return ReverseMemberPointer<&PartialOrderingTheory::m_sets>::reverse(this);
}

SetTheory::SetFlags& PartialOrderingTheory::OrderingSets::setFlags(int_t setId) {
    InternalHandle handle { (uint32_t)setId };
    return theory()->entryAt(handle).flags;
}

SetTheory::SetElements PartialOrderingTheory::OrderingSets::setElements(int_t setId) {
    InternalHandle handle { (uint32_t)setId };
    return theory()->entryAt(handle).literals;
}

BooleanValue PartialOrderingTheory::OrderingSets::getOrCreateOrderingLiteral(Solver& solver, OrderingHandle handle, std::partial_ordering ordering) {
    // 0b00 -> 0b10
    // 0b01 -> 0b01
    // 0b10 -> 0b00
    // 0b11 -> 0b11
    uint32_t idx = poToIndex(ordering);
    idx ^= (~idx & (handle.flipped() ? 1u : 0u)) << 1;

    return getOrCreateElementLiteral(solver, handle.id(), idx);
}

BooleanValue PartialOrderingTheory::OrderingSets::makeElement(Solver& solver, int_t setId, int_t index) {
    auto ordering = poFromIndex(index);
    InternalHandle handle { (uint32_t)setId };
    if (ordering == std::partial_ordering::unordered) {
        return theory()->m_unordered.newLiteral(solver, handle);
    } else if (ordering == std::partial_ordering::equivalent) {
        return theory()->m_equality.newLiteral(solver, handle);
    } else {
        // TODO: Implement strict literals
        VERIFY_NOT_REACHED();
    }
}

// ---------------------- PartialOrderingTheory ---------------------

PartialOrderingTheory::PartialOrderingTheory(Solver& solver, uint64_t baseLabel)
    : m_sets(solver)
    , m_equality(solver, baseLabel + 100)
    , m_unordered(solver, baseLabel + 200) { }

PartialOrderingTheory::OrderingHandle PartialOrderingTheory::order(Solver& solver, Value a, Value b) {
    // Must be compatible with OrientedPair::orient()
    bool flipped = false;
    if (solver.compare(a, b) > 0) {
        std::swap(a, b);
        flipped = true;
    }

    int_t oldSize = m_entries.size();
    InternalHandle handle { (uint32_t)m_entries.get(solver, Link { a, b }) };
    if (m_entries.size() != oldSize) {
        auto orderings = possibleOrderings(solver, a, b);
        for (int_t poIndex = 0; poIndex < 4; poIndex++) {
            if (!orderings.test(poFromIndex(poIndex)))
                m_sets.unitDeactivateElement(solver, handle.id(), poIndex);
        }
    }

    return OrderingHandle { handle, flipped };
}

BooleanValue PartialOrderingTheory::literal(Solver& solver, OrderingHandle handle, std::partial_ordering ordering) {
    return m_sets.getOrCreateOrderingLiteral(solver, handle, ordering);
}

BooleanValue PartialOrderingTheory::equality(Solver& solver, Value a, Value b) {
    return m_sets.getOrCreateOrderingLiteral(solver, order(solver, a, b), std::partial_ordering::equivalent);
}

BooleanValue PartialOrderingTheory::less(Solver& solver, Value a, Value b) {
    return m_sets.getOrCreateOrderingLiteral(solver, order(solver, a, b), std::partial_ordering::less);
}

BooleanValue PartialOrderingTheory::greater(Solver& solver, Value a, Value b) {
    return m_sets.getOrCreateOrderingLiteral(solver, order(solver, a, b), std::partial_ordering::greater);
}

BooleanValue PartialOrderingTheory::unordered(Solver& solver, Value a, Value b) {
    return m_sets.getOrCreateOrderingLiteral(solver, order(solver, a, b), std::partial_ordering::unordered);
}

void PartialOrderingTheory::propagateAssignment(Solver& solver, InternalHandle handle, std::partial_ordering ordering, bool active) {
    m_sets.propagateAssignment(solver, handle.id(), poToIndex(ordering), active);
}

void PartialOrderingTheory::unapplyAssignment(Solver& solver, InternalHandle handle, std::partial_ordering ordering, bool active) {
    m_sets.unapplyAssignment(solver, handle.id(), poToIndex(ordering), active);
}

void PartialOrderingTheory::reapplyAssignment(Solver&, InternalHandle, std::partial_ordering, bool) { }

bool PartialOrderingTheory::isOrderingActive(Solver& solver, Value a, Value b) {
    return solver.isActive(a) && solver.isActive(b);
}

void PartialOrderingTheory::collectOrderingInactiveReasons(Solver& solver, Value a, Value b, std::vector<BooleanValue>& clause) {
    solver.collectInactiveReasons(a, clause);
    solver.collectInactiveReasons(b, clause);
}

bool PartialOrderingTheory::isOrderingActive(Solver& solver, InternalHandle handle) {
    auto [a, b] = linkAt(handle);
    return isOrderingActive(solver, a, b);
}

void PartialOrderingTheory::collectOrderingInactiveReasons(Solver& solver, InternalHandle handle, std::vector<BooleanValue>& clause) {
    auto [a, b] = linkAt(handle);
    collectOrderingInactiveReasons(solver, a, b, clause);
}

}