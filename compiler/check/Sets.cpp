#include <check/Sets.h>

#include <check/SatSolver.h>

namespace check {

// ---------------------------- SetTheory ---------------------------

SetTheory::SetTheory(Solver& solver)
    : ReasonTheory(solver, true) { }

BooleanValue SetTheory::getOrCreateElementLiteral(Solver& solver, int_t setId, int_t index) {
    return getOrCreateElementLiteral(solver, setId, setFlags(setId), setElements(setId), index);
}

BooleanValue SetTheory::getOrCreateElementLiteral(Solver& solver, int_t setId, SetFlags& flags, SetElements elements, int_t index) {
    auto& element = elements[index];
    if (!element.has_value()) {
        element = makeElement(solver, setId, index);

        if (flags.activeElementIndex != INVALID_ACTIVE_INDEX) {
            VERIFY(index != flags.activeElementIndex);
            auto activeElement = elements[flags.activeElementIndex];
            VERIFY(activeElement.has_value());
            solver.assignTrue(solver.negate(element.value()), makeOtherElementActiveReason(activeElement.value()));
        }
    }
    return element.value();
}

void SetTheory::incrementInactiveCount(Solver& solver, int_t setId) {
    SetFlags& flags = setFlags(setId);
    SetElements elements = setElements(setId);

    flags.inactiveElementCount += 1;
    if (flags.inactiveElementCount == elements.size() - 1 && flags.activeElementIndex == INVALID_ACTIVE_INDEX) {
        // Find the last element that is not inactive
        for (int_t index = 0; index < (int_t)elements.size(); index++) {
            auto element = getOrCreateElementLiteral(solver, setId, flags, elements, index);
            if (!solver.assignedFalse(element)) {
                solver.assignTrue(element, makeAllOtherInactiveReason(setId, index));
                break;
            }
        }
    }
}

void SetTheory::unitDeactivateElement(Solver& solver, int_t setId, int_t index) {
    SetElements elements = setElements(setId);
    VERIFY(!elements[index].has_value());
    elements[index] = builtins::false_literal;
    incrementInactiveCount(solver, setId);
}

void SetTheory::propagateAssignment(Solver& solver, int_t setId, int_t assignedIndex, bool active) {
    if (active) {
        SetFlags& flags = setFlags(setId);
        SetElements elements = setElements(setId);
        VERIFY(flags.activeElementIndex == INVALID_ACTIVE_INDEX);
        flags.activeElementIndex = assignedIndex;
        onElementActivated(solver, setId, assignedIndex);

        for (int_t otherIndex = 0; otherIndex < (int_t)elements.size(); otherIndex++) {
            auto otherElement = elements[otherIndex];
            if (otherIndex == assignedIndex || !otherElement.has_value())
                continue;

            solver.assignTrue(
                solver.negate(otherElement.value()),
                makeOtherElementActiveReason(elements[assignedIndex].value()));
        }
    } else {
        incrementInactiveCount(solver, setId);
    }
}

void SetTheory::unapplyAssignment(Solver&, int_t setId, int_t index, bool active) {
    auto& flags = setFlags(setId);
    if (active) {
        VERIFY(flags.activeElementIndex == index);
        flags.activeElementIndex = INVALID_ACTIVE_INDEX;
    } else {
        flags.inactiveElementCount -= 1;
    }
}

Reason SetTheory::makeOtherElementActiveReason(BooleanValue activeElement) {
    return Reason { (uint32_t)theoryId(), 0, std::bit_cast<uint32_t>(activeElement) };
}

Reason SetTheory::makeAllOtherInactiveReason(int_t setId, int_t activeIndex) {
    return Reason { (uint32_t)theoryId(), 1, (uint32_t)setId, (uint32_t)activeIndex };
}

bool SetTheory::isAllOtherInactiveReason(const Reason& reason) { return reason.data0 != 0; }

BooleanValue SetTheory::reasonActiveElementLiteral(const Reason& reason) {
    return std::bit_cast<BooleanValue>(reason.data1);
}

int_t SetTheory::reasonActiveElementIndex(const Reason& reason) { return reason.data2; }

int_t SetTheory::reasonSetId(const Reason& reason) { return reason.data1; }

bool SetTheory::testReason(Solver& solver, BooleanValue, const Reason& reason) {
    if (isAllOtherInactiveReason(reason)) {
        // Check that all other variables are still inactive
        int_t setId = reasonSetId(reason);
        const SetFlags& flags = setFlags(setId);
        SetElements elements = setElements(setId);
        return flags.inactiveElementCount == elements.size() - 1;
    } else {
        // Check that the active variable is still active
        return solver.assignedTrue(reasonActiveElementLiteral(reason));
    }
}

ReasonTheory::ClauseAndIndex SetTheory::reasonToClause(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) {
    auto& clause = solver.scratchClause();
    if (isAllOtherInactiveReason(reason)) {
        int_t setId = reasonSetId(reason);
        for (auto element : setElements(setId))
            clause.push_back(element.value());
        return { clause, reasonActiveElementIndex(reason) };
    } else {
        clause.push_back(solver.negate(reasonActiveElementLiteral(reason)));
        clause.push_back(assignedLiteral);
        return { clause, 1 };
    }
}

// --------------------------- DynamicSets --------------------------

DynamicSets::DynamicSets(Solver& solver)
    : SimpleBooleanTheory(solver), SetTheory(solver) { }

std::string DynamicSets::formatPositiveLiteral(Solver& solver, int_t varId) {
    const auto& info = variables[varId];
    return formatElement(solver, info.setId, info.indexInSet);
}

std::string DynamicSets::formatNegativeLiteral(Solver& solver, int_t varId) {
    return "!" + formatPositiveLiteral(solver, varId);
}

uint32_t DynamicSets::labelOfVariable(Solver& solver, int_t varId) {
    const auto& info = variables[varId];
    return labelOfElement(solver, info.setId, info.indexInSet);
}

int_t DynamicSets::newSet(Solver&, int_t setSize) {
    VERIFY(setSize > 1); // TODO: Sets of size one could be ok, but we would have to assign its only element immediately
    int_t setId = setCount();
    sets.emplace_back(setSize);
    return setId;
}
BooleanValue DynamicSets::makeElement(Solver& solver, int_t setId, int_t index) {
    VERIFY((int_t)variables.size() == variableCount());
    int_t varId = newVariable(solver);
    variables.push_back({ (uint32_t)setId, (uint32_t)index });
    return positiveLiteral(varId);
}

void DynamicSets::propagateAssignment(Solver& solver, BooleanValue lit) {
    auto varInfo = variables[variableId(lit)];
    SetTheory::propagateAssignment(solver, varInfo.setId, varInfo.indexInSet, isPositive(lit));
}

void DynamicSets::unapplyAssignment(Solver& solver, BooleanValue lit) {
    auto varInfo = variables[variableId(lit)];
    SetTheory::unapplyAssignment(solver, varInfo.setId, varInfo.indexInSet, isPositive(lit));
}

}