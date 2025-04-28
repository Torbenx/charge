#include <check/PartialOrdering.h>

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

// ---------------------- PartialOrderingTheory ---------------------

PartialOrderingTheory::PartialOrderingTheory(Solver& solver, uint64_t baseLabel)
    : m_entries {
        { builtins::true_literal, builtins::false_literal, builtins::false_literal, builtins::false_literal },
        { builtins::false_literal, builtins::true_literal, builtins::false_literal, builtins::false_literal },
        { builtins::false_literal, builtins::false_literal, builtins::true_literal, builtins::false_literal },
        { builtins::false_literal, builtins::false_literal, builtins::false_literal, builtins::true_literal },
    }
    , m_equality(solver, baseLabel + 100)
    , m_unordered(solver, baseLabel + 200) { }

}