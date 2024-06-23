#pragma once

#include <check/LiteralInfo.h>

namespace check {

struct BooleanVariables : SimpleBooleanTheory<> {
    using SimpleBooleanTheory<>::SimpleBooleanTheory;

    std::string formatPositiveLiteral(Solver&, int_t varId) override {
        return std::to_string(varId);
    }
    std::string formatNegativeLiteral(Solver&, int_t varId) override {
        auto result = std::to_string(varId);
        result.insert(result.begin(), '-');
        return result;
    }

    void propagateFalseAssignment(Solver&, BooleanValue) override { }
    void reapplyFalseAssignment(Solver&, BooleanValue) override { }
    void unapplyFalseAssignment(Solver&, BooleanValue) override { }

    BooleanValue literalFromSign(int_t var) const { return var < 0 ? negativeLiteral(-var) : positiveLiteral(var); }
};

}