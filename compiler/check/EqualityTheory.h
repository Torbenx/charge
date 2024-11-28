#pragma once

#include <check/LiteralInfo.h>
#include <check/SimpleBooleanTheory.h>

#include <FlatTreeSet.h>

namespace check {

struct EqualityTheory : SimpleBooleanTheory {
    using SimpleBooleanTheory::SimpleBooleanTheory;

    //! Returns a value that is true if and only if \p a == \p b
    BooleanValue equality(Solver& solver, Value a, Value b) {
        if (a == b)
            return builtins::true_literal;
        return positiveLiteral(equalityVariable(solver, a, b));
    }

    //! Returns a value that is true if and only if \p a != \p b
    BooleanValue disequality(Solver& solver, Value a, Value b) {
        if (a == b)
            return builtins::false_literal;
        return negativeLiteral(equalityVariable(solver, a, b));
    }

    std::string formatPositiveLiteral(Solver&, int_t varId) override;
    std::string formatNegativeLiteral(Solver&, int_t varId) override;

    uint64_t labelOfValue(Solver&, Value v) override {
        BooleanValue lit { v };
        return baseLabel + (uint64_t)equalities.label(variableId(lit)) * 2 + isPositive(lit);
    }

protected:
    struct Link {
        Value source;
        Value target;

        bool operator==(const Link&) const = default;
    };

    struct LinkSet : FlatTreeSetDetail::Base<LinkSet, Link> {
        uint32_t get(Solver& solver, const Link& link) {
            return Base::get(solver, link);
        }

    private:
        friend Base;
        uint32_t makeNode(Solver&, const Link& link, TreeLabel label);
        std::strong_ordering compare(Solver& solver, const Link& a, const Link& b);
    };

    int_t equalityVariable(Solver&, Value a, Value b);

    static Link orient(Solver&, Value a, Value b);

    virtual void onNewVariable(Solver&, int_t eqId) = 0;

    LinkSet equalities;
    uint64_t baseLabel = 0;
};

}