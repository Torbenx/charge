#pragma once

#include <check/LiteralInfo.h>

namespace check {

struct SimpleBooleanTheory : BooleanTheory {
    using BooleanTheory::BooleanTheory;

    BooleanValue negate(Solver&, BooleanValue lit) override {
        return negate(lit);
    }
    BooleanValue negate(BooleanValue lit) {
        return { lit.theoryId, lit.valueId ^ 1u };
    }
    LiteralInfo& literalInfo(Solver&, BooleanValue lit) override {
        return literalInfo(lit);
    }
    LiteralInfo& literalInfo(BooleanValue lit) { return infos[lit.valueId]; }

    bool assignedPositive(Solver&, int_t varId);
    bool assignedNegative(Solver&, int_t varId);

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

    virtual void collectVariableInactiveReasons(Solver&, int_t, std::vector<BooleanValue>&) { }
    virtual bool isVariableActive(Solver&, int_t) { return true; }

    void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
        collectVariableInactiveReasons(solver, variableId({ v }), clause);
    }
    bool isValueActive(Solver& solver, Value v) override {
        return isVariableActive(solver, variableId({ v }));
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = find; i < variableCount(); i++) {
            f(positiveLiteral(i));
            f(negativeLiteral(i));
        }
    }

    int_t newVariable() {
        int_t id = variableCount();
        infos.resize(infos.size() + 2);
        return id;
    }

    std::optional<int_t> findUnassignedVariable() {
        for (int_t i = find; i < variableCount(); i++) {
            if (literalInfo(positiveLiteral(i)).tentativelyFalse() || literalInfo(negativeLiteral(i)).tentativelyFalse())
                continue;
            find = i;
            return i;
        }
        for (int_t i = 0; i < find; i++) {
            if (literalInfo(positiveLiteral(i)).tentativelyFalse() || literalInfo(negativeLiteral(i)).tentativelyFalse())
                continue;
            find = i;
            return i;
        }
        return std::nullopt;
    }

    int_t variableId(BooleanValue v) const { return v.valueId >> 1; }
    bool isPositive(BooleanValue v) const { return (v.valueId & 1u) == 0u; }

    int_t variableCount() const { return infos.size() >> 1; }

    BooleanValue positiveLiteral(int_t varId) const { return { (uint32_t)theoryId(), (uint32_t)varId * 2u }; }
    BooleanValue negativeLiteral(int_t varId) const { return { (uint32_t)theoryId(), (uint32_t)varId * 2u + 1u }; }

private:
    std::vector<LiteralInfo> infos;
    int_t find = 0;
};

}
