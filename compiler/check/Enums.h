#pragma once

#include <check/Reason.h>
#include <check/SimpleBooleanTheory.h>

namespace check {

struct SetElements : SimpleBooleanTheory, private ReasonTheory {
    SetElements(Solver&);

    BooleanValue elementActiveLiteral(Solver& solver, int_t setId, int_t index) {
        return positiveLiteral(getOrCreateVariable(solver, setId, index));
    }
    BooleanValue elementInactiveLiteral(Solver& solver, int_t setId, int_t index) {
        return negativeLiteral(getOrCreateVariable(solver, setId, index));
    }
    bool hasActiveElement(int_t setId) {
        return sets[setId].activeElementIndex != INVALID_ACTIVE_INDEX;
    }
    int_t activeElement(int_t setId) {
        VERIFY(hasActiveElement(setId));
        return sets[setId].activeElementIndex;
    }

    int_t newSet(Solver&, int_t setSize);

    std::string formatPositiveLiteral(Solver&, int_t varId) override;
    std::string formatNegativeLiteral(Solver& solver, int_t varId) override;
    uint64_t labelOfValue(Solver&, Value) override;

    void propagateFalseAssignment(Solver&, BooleanValue) override;
    void reapplyFalseAssignment(Solver&, BooleanValue) override { }
    void unapplyFalseAssignment(Solver&, BooleanValue) override;

    int_t setCount() const { return sets.size(); }

    virtual void onElementActivated(Solver&, int_t setId, int_t index) = 0;

    virtual std::string formatElement(Solver&, int_t setId, int_t index) = 0;
    virtual uint64_t labelOfElement(Solver&, int_t setId, int_t index, bool positive) = 0;

private:
    static constexpr uint32_t INVALID_ACTIVE_INDEX = -1;
    static constexpr uint32_t INVALID_VARIABLE_ID = -1;

    struct SetInfo {
        uint32_t activeElementIndex = INVALID_ACTIVE_INDEX;
        uint32_t inactiveElementCount = 0;
        std::vector<uint32_t> variableIds;

        explicit SetInfo(int_t setSize)
            : variableIds(setSize, INVALID_VARIABLE_ID) { }

        int_t setSize() const { return variableIds.size(); }
    };

    struct VariableInfo {
        uint32_t setId;
        uint32_t indexInSet;
    };

    int_t getOrCreateVariable(Solver& solver, int_t setId, int_t index);
    int_t getVariable(int_t setId, int_t index);

    Reason makeOtherVarActiveReason(int_t activeVarId, int_t inactiveVarId);
    Reason makeAllOtherInactiveReason(int_t setId, int_t activeIndex);
    bool isAllOtherInactiveReason(const Reason&);
    int_t reasonActiveVarId(const Reason&);
    int_t reasonInactiveVarId(const Reason&);
    int_t reasonSetId(const Reason&);
    int_t reasonActiveIndex(const Reason&);

    bool testReason(Solver&, const Reason&) override;
    ClauseAndIndex reasonToClause(Solver&, const Reason&) override;
    void newDecisionLevel(Solver&) override { }
    void backtrack(Solver&) override { }

    std::vector<SetInfo> sets;
    std::vector<VariableInfo> variables;
};

}