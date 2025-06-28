#pragma once

#include <check/Reason.h>
#include <check/SimpleBooleanTheory.h>

#include <span>

namespace check {

struct SetTheory : private ReasonTheory {
    static constexpr uint32_t INVALID_ACTIVE_INDEX = -1;

    struct SetFlags {
        uint32_t activeElementIndex = INVALID_ACTIVE_INDEX;
        uint32_t inactiveElementCount = 0;
    };
    using SetElements = std::span<std::optional<BooleanValue>>;

    SetTheory(Solver&);

    bool hasActiveElement(int_t setId) {
        return setFlags(setId).activeElementIndex != INVALID_ACTIVE_INDEX;
    }
    int_t activeElement(int_t setId) {
        const auto& flags = setFlags(setId);
        VERIFY(flags.activeElementIndex != INVALID_ACTIVE_INDEX);
        return flags.activeElementIndex;
    }

protected:
    virtual SetFlags& setFlags(int_t setId) = 0;
    virtual SetElements setElements(int_t setId) = 0;

    virtual BooleanValue makeElement(Solver&, int_t setId, int_t index) = 0;

    virtual void onElementActivated(Solver&, [[maybe_unused]] int_t setId, [[maybe_unused]] int_t index) { }

    void unitDeactivateElement(Solver&, int_t setId, int_t index);
    void propagateAssignment(Solver&, int_t setId, int_t index, bool active);
    void unapplyAssignment(Solver&, int_t setId, int_t index, bool active);

    BooleanValue getOrCreateElementLiteral(Solver& solver, int_t setId, int_t index);

private:
    void incrementInactiveCount(Solver& solver, int_t setId);
    BooleanValue getOrCreateElementLiteral(Solver& solver, int_t setId, SetFlags& flags, SetElements elements, int_t index);

    Reason makeOtherElementActiveReason(BooleanValue activeElement);
    Reason makeAllOtherInactiveReason(int_t setId, int_t activeIndex);
    BooleanValue reasonActiveElementLiteral(const Reason&);
    int_t reasonActiveElementIndex(const Reason&);
    int_t reasonSetId(const Reason&);
    bool isAllOtherInactiveReason(const Reason&);

    bool testReason(Solver&, BooleanValue, const Reason&) override;
    ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&) override;
    void newDecisionLevel(Solver&) override { }
    void backtrack(Solver&) override { }
};

struct DynamicSets : SimpleBooleanTheory, SetTheory {
    DynamicSets(Solver&, uint64_t baseLabel);

    BooleanValue elementActiveLiteral(Solver& solver, int_t setId, int_t index) {
        return getOrCreateElementLiteral(solver, setId, index);
    }
    BooleanValue elementInactiveLiteral(Solver& solver, int_t setId, int_t index) {
        return negate(solver, getOrCreateElementLiteral(solver, setId, index));
    }

    int_t newSet(Solver&, int_t setSize);

    std::string formatPositiveLiteral(Solver&, int_t varId) override;
    std::string formatNegativeLiteral(Solver& solver, int_t varId) override;
    uint32_t labelOfVariable(Solver&, int_t varId) override;

    void propagateAssignment(Solver&, BooleanValue) override;
    void reapplyAssignment(Solver&, BooleanValue) override { }
    void unapplyAssignment(Solver&, BooleanValue) override;

    int_t setCount() const { return sets.size(); }

    virtual std::string formatElement(Solver&, int_t setId, int_t index) = 0;
    virtual uint32_t labelOfElement(Solver&, int_t setId, int_t index) = 0;

private:
    struct SetInfo {
        SetFlags flags;
        std::vector<std::optional<BooleanValue>> elements;

        explicit SetInfo(int_t setSize)
            : elements(setSize, std::nullopt) { }
    };

    struct VariableInfo {
        uint32_t setId;
        uint32_t indexInSet;
    };

    SetFlags& setFlags(int_t setId) override { return sets[setId].flags; }
    SetElements setElements(int_t setId) override { return sets[setId].elements; }
    BooleanValue makeElement(Solver&, int_t setId, int_t index) override;

    std::vector<SetInfo> sets;
    std::vector<VariableInfo> variables;
};

}