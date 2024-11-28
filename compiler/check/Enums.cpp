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
            solver.assignTrue(negativeLiteral(varElm), makeOtherVarActiveReason(activeVarId, varElm));
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

void SetElements::propagateFalseAssignment(Solver& solver, BooleanValue value) {
    int_t assignedVarId = variableId(value);
    const auto& varInfo = variables[assignedVarId];
    auto& setInfo = sets[varInfo.setId];

    if (isPositive(value)) {
        setInfo.inactiveElementCount += 1;
        if (setInfo.inactiveElementCount == setInfo.setSize() - 1 && setInfo.activeElementIndex == INVALID_ACTIVE_INDEX) {
            for (int_t index = 0; index < setInfo.setSize(); index++) {
                int_t varId = getOrCreateVariable(solver, varInfo.setId, index);
                if (!assignedNegative(solver, varId)) {
                    solver.assignTrue(positiveLiteral(varId), makeAllOtherInactiveReason(varInfo.setId, index));
                    break;
                }
            }
        }
    } else {
        VERIFY(setInfo.activeElementIndex == INVALID_ACTIVE_INDEX);
        setInfo.activeElementIndex = varInfo.indexInSet;
        onElementActivated(solver, varInfo.setId, varInfo.indexInSet);

        for (uint32_t varId : setInfo.variableIds) {
            if (varId == assignedVarId || varId == INVALID_VARIABLE_ID)
                continue;

            solver.assignTrue(
                negativeLiteral(varId),
                makeOtherVarActiveReason(assignedVarId, varId));
        }
    }
}

void SetElements::unapplyFalseAssignment(Solver&, BooleanValue value) {
    int_t activeVarId = variableId(value);
    const auto& varInfo = variables[activeVarId];
    auto& setInfo = sets[varInfo.setId];

    if (isPositive(value)) {
        setInfo.inactiveElementCount -= 1;
    } else {
        VERIFY(setInfo.activeElementIndex == varInfo.indexInSet);
        setInfo.activeElementIndex = INVALID_ACTIVE_INDEX;
    }
}

Reason SetElements::makeOtherVarActiveReason(int_t activeVarId, int_t inactiveVarId) {
    return Reason { (uint32_t)ReasonTheory::theoryId(), 0, (uint32_t)activeVarId, (uint32_t)inactiveVarId };
}

Reason SetElements::makeAllOtherInactiveReason(int_t setId, int_t activeVarId) {
    return Reason { (uint32_t)ReasonTheory::theoryId(), 1, (uint32_t)setId, (uint32_t)activeVarId };
}
bool SetElements::isAllOtherInactiveReason(const Reason& reason) { return reason.data0; }
int_t SetElements::reasonActiveVarId(const Reason& reason) { return reason.data1; }
int_t SetElements::reasonInactiveVarId(const Reason& reason) { return reason.data2; }
int_t SetElements::reasonSetId(const Reason& reason) { return reason.data1; }
int_t SetElements::reasonActiveIndex(const Reason& reason) { return reason.data2; }

bool SetElements::testReason(Solver& solver, const Reason& reason) {
    if (isAllOtherInactiveReason(reason)) {
        int_t setId = reasonSetId(reason);
        const auto& setInfo = sets[setId];
        return setInfo.inactiveElementCount == setInfo.setSize() - 1
            && !solver.assignedFalse(positiveLiteral(getVariable(setId, reasonActiveIndex(reason))));
    } else {
        return assignedPositive(solver, reasonActiveVarId(reason));
    }
}

ReasonTheory::ClauseAndIndex SetElements::reasonToClause(Solver& solver, const Reason& reason) {
    auto& clause = solver.scratchClause();
    if (isAllOtherInactiveReason(reason)) {
        int_t setId = reasonSetId(reason);
        const auto& setInfo = sets[setId];
        for (int_t index = 0; index < setInfo.setSize(); index++)
            clause.push_back(positiveLiteral(getVariable(setId, index)));
        return { clause, reasonActiveIndex(reason) };
    } else {
        clause.push_back(negativeLiteral(reasonActiveVarId(reason)));
        clause.push_back(negativeLiteral(reasonInactiveVarId(reason)));
        return { clause, 1 };
    }
}

}