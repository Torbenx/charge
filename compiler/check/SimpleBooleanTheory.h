#pragma once

#include <check/LiteralInfo.h>

namespace check {

struct SimpleBooleanTheory : BooleanTheory {
    SimpleBooleanTheory(Solver& solver)
        : BooleanTheory(solver)
        // Note: Labels don't actually matter for bools since they are not used by equality
        //       so we can pass any ValueCategory here.
        , baseLabel(solver, ValueCategory::Expression) { }

    bool assignedPositive(Solver&, int_t varId);
    bool assignedNegative(Solver&, int_t varId);

    int_t variableCount(Solver& solver);
    int_t newVariable(Solver& solver) { return variableId(newBoolean(solver)); }

    virtual std::string formatPositiveLiteral(Solver&, int_t varId) = 0;
    virtual std::string formatNegativeLiteral(Solver&, int_t varId) = 0;
    std::string formatValue(Solver& solver, Value v) override {
        auto lit = BooleanValue { v };
        int_t varId = variableId(lit);
        if (isPositive(lit))
            return formatPositiveLiteral(solver, varId);
        else
            return formatNegativeLiteral(solver, varId);
    }

    virtual uint32_t labelOfVariable(Solver&, int_t varId) = 0;
    uint64_t labelOfValue(Solver& solver, Value v) override {
        BooleanValue lit { v };
        return baseLabel + (uint64_t)labelOfVariable(solver, variableId(lit)) * 2 + isPositive(lit);
    }

    virtual void collectVariableInactiveReasons(Solver&, int_t, std::vector<BooleanValue>&) { }
    virtual bool isVariableActive(Solver&, int_t) { return true; }

    void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
        collectVariableInactiveReasons(solver, variableId({ v }), clause);
    }
    bool isValueActive(Solver& solver, Value v) override {
        return isVariableActive(solver, variableId({ v }));
    }

    std::optional<int_t> findUnassignedVariable(Solver&);

    int_t variableId(BooleanValue v) const { return v.valueId >> 1; }
    bool isPositive(BooleanValue v) const { return (v.valueId & 1u) == 0u; }

    BooleanValue positiveLiteral(int_t varId) const { return { (uint32_t)theoryId(), (uint32_t)varId * 2u }; }
    BooleanValue negativeLiteral(int_t varId) const { return { (uint32_t)theoryId(), (uint32_t)varId * 2u + 1u }; }

private:
    int_t find = 0;
    ValueBaseLabel baseLabel;
};

}
