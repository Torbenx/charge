#pragma once

#include <check/SimpleBooleanTheory.h>

namespace check {

struct BooleanVariables : SimpleBooleanTheory {
    BooleanVariables(Solver& solver, uint64_t baseLabel)
        : SimpleBooleanTheory(solver), m_baseLabel(baseLabel) { }

    std::string formatPositiveLiteral(Solver&, int_t varId) override {
        return std::to_string(varId);
    }
    std::string formatNegativeLiteral(Solver&, int_t varId) override {
        auto result = std::to_string(varId);
        result.insert(result.begin(), '-');
        return result;
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        BooleanValue lit { v };
        return m_baseLabel + variableId(lit) * 2 + isPositive(lit);
    }

    void propagateFalseAssignment(Solver&, BooleanValue) override { }
    void reapplyFalseAssignment(Solver&, BooleanValue) override { }
    void unapplyFalseAssignment(Solver&, BooleanValue) override { }

    BooleanValue literalFromSign(int_t var) const { return var < 0 ? negativeLiteral(-var) : positiveLiteral(var); }

    uint64_t m_baseLabel;
};

}