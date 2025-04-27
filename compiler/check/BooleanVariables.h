#pragma once

#include <check/SimpleBooleanTheory.h>

namespace check {

struct BooleanVariables : SimpleBooleanTheory {
    BooleanVariables(Solver& solver, uint64_t baseLabel)
        : SimpleBooleanTheory(solver, baseLabel) { }

    std::string formatPositiveLiteral(Solver&, int_t varId) override {
        return std::to_string(varId);
    }
    std::string formatNegativeLiteral(Solver&, int_t varId) override {
        auto result = std::to_string(varId);
        result.insert(result.begin(), '-');
        return result;
    }

    uint32_t labelOfVariable(Solver&, int_t varId) override {
        return varId;
    }

    void propagateAssignment(Solver&, BooleanValue) override { }
    void reapplyAssignment(Solver&, BooleanValue) override { }
    void unapplyAssignment(Solver&, BooleanValue) override { }

    BooleanValue literalFromSign(int_t var) const { return var < 0 ? negativeLiteral(-var) : positiveLiteral(var); }
};

}