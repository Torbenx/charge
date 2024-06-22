#pragma once

#include <check/EqualityTheory.h>

namespace check {

//! Theory for equality of boolean values
/*!
Equalities are eagerly encoded as clauses. For each equality there will be 4 clauses:
    a != b || !a ||  b
    a != b ||  a || !b
    a == b ||  a ||  b
    a == b || !a || !b
*/
struct BooleanEquality : EqualityTheory {
    void onNewVariable(Solver& solver, int_t varId) override {
        Link l = equalities.at(varId);
        int_t var = newVariable();
        BooleanValue eq = positiveLiteral(var);
        BooleanValue neq = negativeLiteral(var);
        BooleanValue a { l.source };
        BooleanValue b { l.target };
        BooleanValue na = solver.negate(a);
        BooleanValue nb = solver.negate(b);

        if (a == b) {
            solver.addClause({ eq });
        } else if (a == nb) {
            solver.addClause({ neq });
        } else {
            solver.addClause({ neq, na, b });
            solver.addClause({ neq, a, nb });
            solver.addClause({ eq, a, b });
            solver.addClause({ eq, na, nb });
        }
    }

    void propagateFalseAssignment(Solver&, BooleanValue) override { }
};

}