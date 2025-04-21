#include <check/Enums.h>

#include <check/SatSolver.h>

namespace check {

// --------------------------- SetElements --------------------------

SetElements::SetElements(Solver& solver)
    : SimpleBooleanTheory(solver), ReasonTheory(solver, true) { }

std::string SetElements::formatPositiveLiteral(Solver& solver, int_t varId) {
    const auto& info = variables[varId];
    return formatElement(solver, info.setId, info.indexInSet);
}

std::string SetElements::formatNegativeLiteral(Solver& solver, int_t varId) {
    return "!" + formatPositiveLiteral(solver, varId);
}

uint64_t SetElements::labelOfValue(Solver& solver, Value v) {
    BooleanValue lit { v };
    const auto& info = variables[variableId(lit)];
    return labelOfElement(solver, info.setId, info.indexInSet, isPositive(lit));
}

int_t SetElements::getVariable(int_t setId, int_t index) {
    uint32_t varId = sets[setId].variableIds[index];
    VERIFY(varId != INVALID_VARIABLE_ID);
    return varId;
}

int_t SetElements::getOrCreateVariable(Solver& solver, int_t setId, int_t index) {
    auto& setInfo = sets[setId];
    auto& varElm = setInfo.variableIds[index];
    if (varElm == INVALID_VARIABLE_ID) {
        VERIFY((int_t)variables.size() == variableCount());
        varElm = newVariable();
        variables.push_back({ (uint32_t)setId, (uint32_t)index });

        if (setInfo.activeElementIndex != INVALID_ACTIVE_INDEX) {
            VERIFY(index != setInfo.activeElementIndex);
            int_t activeVarId = setInfo.variableIds[setInfo.activeElementIndex];
            solver.assignTrue(negativeLiteral(varElm), makeOtherVarActiveReason(activeVarId));
        }
    }
    return varElm;
}

int_t SetElements::newSet(Solver&, int_t setSize) {
    VERIFY(setSize > 0);
    int_t setId = setCount();
    sets.emplace_back(setSize);
    return setId;
}

void SetElements::propagateAssignment(Solver& solver, BooleanValue value) {
    int_t assignedVarId = variableId(value);
    const auto& varInfo = variables[assignedVarId];
    auto& setInfo = sets[varInfo.setId];

    if (isPositive(value)) {
        VERIFY(setInfo.activeElementIndex == INVALID_ACTIVE_INDEX);
        setInfo.activeElementIndex = varInfo.indexInSet;
        onElementActivated(solver, varInfo.setId, varInfo.indexInSet);

        for (uint32_t varId : setInfo.variableIds) {
            if (varId == assignedVarId || varId == INVALID_VARIABLE_ID)
                continue;

            solver.assignTrue(
                negativeLiteral(varId),
                makeOtherVarActiveReason(assignedVarId));
        }
    } else {
        setInfo.inactiveElementCount += 1;
        if (setInfo.inactiveElementCount == setInfo.setSize() - 1 && setInfo.activeElementIndex == INVALID_ACTIVE_INDEX) {
            for (int_t index = 0; index < setInfo.setSize(); index++) {
                int_t varId = getOrCreateVariable(solver, varInfo.setId, index);
                if (!assignedNegative(solver, varId)) {
                    solver.assignTrue(positiveLiteral(varId), makeAllOtherInactiveReason(varInfo.setId));
                    break;
                }
            }
        }
    }
}

void SetElements::unapplyAssignment(Solver&, BooleanValue value) {
    int_t activeVarId = variableId(value);
    const auto& varInfo = variables[activeVarId];
    auto& setInfo = sets[varInfo.setId];

    if (isPositive(value)) {
        VERIFY(setInfo.activeElementIndex == varInfo.indexInSet);
        setInfo.activeElementIndex = INVALID_ACTIVE_INDEX;
    } else {
        setInfo.inactiveElementCount -= 1;
    }
}

Reason SetElements::makeOtherVarActiveReason(int_t activeVarId) {
    return Reason { (uint32_t)ReasonTheory::theoryId(), 0, (uint32_t)activeVarId, 0 };
}

Reason SetElements::makeAllOtherInactiveReason(int_t setId) {
    return Reason { (uint32_t)ReasonTheory::theoryId(), 0, (uint32_t)setId, 0 };
}
int_t SetElements::reasonActiveVarId(const Reason& reason) { return reason.data1; }
int_t SetElements::reasonSetId(const Reason& reason) { return reason.data1; }

bool SetElements::testReason(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) {
    if (isPositive(assignedLiteral)) {
        // Check that all other variables are still inactive
        int_t setId = reasonSetId(reason);
        const auto& setInfo = sets[setId];
        return setInfo.inactiveElementCount == setInfo.setSize() - 1;
    } else {
        // Check that the active variable is still active
        return assignedPositive(solver, reasonActiveVarId(reason));
    }
}

ReasonTheory::ClauseAndIndex SetElements::reasonToClause(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) {
    auto& clause = solver.scratchClause();
    if (isPositive(assignedLiteral)) {
        int_t setId = reasonSetId(reason);
        const auto& setInfo = sets[setId];
        for (int_t index = 0; index < setInfo.setSize(); index++)
            clause.push_back(positiveLiteral(getVariable(setId, index)));
        return { clause, variables[variableId(assignedLiteral)].indexInSet };
    } else {
        clause.push_back(negativeLiteral(reasonActiveVarId(reason)));
        clause.push_back(assignedLiteral);
        return { clause, 1 };
    }
}

}