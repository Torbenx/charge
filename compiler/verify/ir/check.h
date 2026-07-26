#pragma once

#include <verify/ir/Function.h>

namespace verify::ir {

//! The result of checking a function
struct FunctionCheckReport {
    //! An expression whose precondition is not proven, together with the missing proposition
    struct InvalidExpression {
        Expr expr;
        Bool precondition;
    };

    //! An instruction whose precondition is not proven, together with the missing proposition
    struct InvalidInstruction {
        CodePos pos;
        Bool precondition;
    };

    //! Expressions whose arguments do not have the sorts declared in 'expressions.inc'
    std::vector<Expr> malformedExpressions;

    //! Instructions whose arguments do not have the sorts declared in 'instructions.inc'
    std::vector<CodePos> malformedInstructions;

    //! Expressions whose preconditions are not established
    std::vector<InvalidExpression> invalidExpressions;

    //! Instructions whose preconditions are not established
    std::vector<InvalidInstruction> invalidInstructions;

    //! Theorems whose proof does not establish their proposition
    std::vector<Theorem> invalidProofs;

    //! Theorems that are left to the SMT solver, which the check does not run
    std::vector<Theorem> smtSolveTheorems;

    //! Theorems that have no proof yet
    std::vector<Theorem> sorryTheorems;

    bool ok() const {
        return malformedExpressions.empty() && malformedInstructions.empty()
            && invalidExpressions.empty() && invalidInstructions.empty()
            && invalidProofs.empty() && smtSolveTheorems.empty() && sorryTheorems.empty();
    }
};

//! Validates that a function is well formed
FunctionCheckReport check(Function&);

}
