#pragma once

#include <check/ValueTheory.h>

namespace check {

//! Theory for equality of boolean values
/*!
Equalities are eagerly encoded as clauses. For each equality there will be 4 clauses:
    a != b || !a ||  b
    a != b ||  a || !b
    a == b ||  a ||  b
    a == b || !a || !b
*/
struct BooleanEquality : SimpleBooleanTheory<> {
    void link(Solver& solver, BooleanValue a, BooleanValue b) {
        int_t var = newVariable();
        BooleanValue eq = positiveLiteral(var);
        BooleanValue neq = negativeLiteral(var);
        BooleanValue na = solver.negate(a);
        BooleanValue nb = solver.negate(b);

        solver.addClause({ neq, na, b });
        solver.addClause({ neq, a, nb });
        solver.addClause({ eq, a, b });
        solver.addClause({ eq, na, nb });
    }

    void assignFalse(Solver&, BooleanValue) override { }
    void revertFalseAssignment(Solver&, BooleanValue) override { }

    std::string formatPositiveLiteral(Solver& solver, int_t eqId) override {
        auto eq = equalities[eqId];
        return fmt::format("({} == {})", solver.formatValue(eq.source), solver.formatValue(eq.target));
    }
    std::string formatNegativeLiteral(Solver& solver, int_t eqId) override {
        auto eq = equalities[eqId];
        return fmt::format("({} != {})", solver.formatValue(eq.source), solver.formatValue(eq.target));
    }

    struct Link {
        BooleanValue source;
        BooleanValue target;
    };
    std::vector<Link> equalities;
};

}