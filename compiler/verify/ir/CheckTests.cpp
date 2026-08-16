#include <verify/ir/check.h>

#include <gtest/gtest.h>

namespace verify::ir {

TEST(VerifyIR, CheckEmptyFunction) {
    Function fn;
    EXPECT_TRUE(check(fn).ok());
}

TEST(VerifyIR, CheckExpressionSorts) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));
    Type type(fn.addParameter(Sort::Type));

    fn.addMemoryLocType({ loc });
    fn.addMemoryLocMember({ loc });
    fn.addTypeLoad({ loc, CodePos(0) });
    EXPECT_TRUE(check(fn).malformedExpressions.empty());

    // 'MemoryLocType' expects a location, not a type
    Expr wrongArgument = fn.addMemoryLocType({ MemoryLoc(type) });
    EXPECT_EQ(check(fn).malformedExpressions, std::vector<Expr> { wrongArgument });
}

TEST(VerifyIR, CheckVariadicExpressionSorts) {
    Function fn;
    Bool cond(fn.addParameter(Sort::Bool));
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));

    fn.addAnd({ cond, !cond });
    fn.addOr({ cond, Bool(fn.addEquality({ loc, loc })) });
    EXPECT_TRUE(check(fn).malformedExpressions.empty());

    // Operands are stored as plain expressions, so a location among them is only caught here
    Expr wrongOperand = fn.addAnd({ cond, Bool(loc) });
    EXPECT_EQ(check(fn).malformedExpressions, std::vector<Expr> { wrongOperand });
}

TEST(VerifyIR, CheckExpressionSortsOfLoads) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));
    Type type(fn.addParameter(Sort::Type));

    // The sort a load produces is unrelated to the sort of the location it loads from
    fn.addBoolLoad({ loc, CodePos(0) });
    fn.addMemoryLocLoad({ loc, CodePos(0) });
    EXPECT_TRUE(check(fn).malformedExpressions.empty());

    Expr wrongLocation = fn.addBoolLoad({ MemoryLoc(type), CodePos(0) });
    EXPECT_EQ(check(fn).malformedExpressions, std::vector<Expr> { wrongLocation });
}

TEST(VerifyIR, CheckInstructionSorts) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));
    Bool cond(fn.addParameter(Sort::Bool));

    // The stored value is a plain expression, so it accepts any sort
    fn.addStore({ loc, cond });
    fn.addStore({ loc, loc });
    fn.addBranch({ cond, CodePos(0), CodePos(0) });
    EXPECT_TRUE(check(fn).malformedInstructions.empty());

    // 'Store' expects a location and 'Branch' a boolean condition
    fn.addStore({ MemoryLoc(cond), cond });
    fn.addBranch({ Bool(loc), CodePos(0), CodePos(0) });
    EXPECT_EQ(check(fn).malformedInstructions, (std::vector<CodePos> { CodePos(3), CodePos(4) }));
}

TEST(VerifyIR, CheckInstructionSortsWithoutArguments) {
    Function fn;

    // Instructions whose arguments are not expressions have no sort to check
    fn.addJump({ CodePos(0) });
    fn.addPhi(std::array { CodePos(0) });
    EXPECT_TRUE(check(fn).malformedInstructions.empty());
}

TEST(VerifyIR, CheckLoadPrecondition) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));
    Type locType = fn.addMemoryLocType({ loc });
    MemoryLoc other(fn.addParameter(Sort::MemoryLoc));
    Type otherType = fn.addMemoryLocType({ other });

    Expr load = fn.addTypeLoad({ loc, CodePos(0) });
    auto report = check(fn);
    ASSERT_EQ(report.invalidExpressions.size(), 1u);
    EXPECT_EQ(report.invalidExpressions[0].expr, load);
    EXPECT_EQ(report.invalidExpressions[0].precondition, fn.addScalarType({ locType, Sort::Type }));

    // The proposition has to hold for the loaded location and the loaded sort
    fn.addTheorem(fn.addScalarType({ locType, Sort::Bool }), CodePos(0), Proof::makeSorry());
    fn.addTheorem(fn.addScalarType({ otherType, Sort::Type }), CodePos(0), Proof::makeSorry());
    EXPECT_EQ(check(fn).invalidExpressions.size(), 1u);

    fn.addTheorem(fn.addScalarType({ locType, Sort::Type }), CodePos(0), Proof::makeSorry());
    EXPECT_TRUE(check(fn).invalidExpressions.empty());
}

TEST(VerifyIR, CheckStorePrecondition) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));
    Type locType = fn.addMemoryLocType({ loc });
    Type value(fn.addParameter(Sort::Type));

    // A store requires the location to hold the sort of the stored value
    fn.addStore({ loc, value });
    auto report = check(fn);
    ASSERT_EQ(report.invalidInstructions.size(), 1u);
    EXPECT_EQ(report.invalidInstructions[0].pos, CodePos(0));
    EXPECT_EQ(report.invalidInstructions[0].precondition, fn.addScalarType({ locType, Sort::Type }));

    fn.addTheorem(fn.addScalarType({ locType, Sort::Type }), CodePos(0), Proof::makeSorry());
    EXPECT_TRUE(check(fn).invalidInstructions.empty());
}

TEST(VerifyIR, CheckEqualitySorts) {
    Function fn;
    Expr loc = fn.addParameter(Sort::MemoryLoc);
    Expr type = fn.addParameter(Sort::Type);

    // An equality is the one expression that accepts any sort on both of its sides
    fn.addEquality({ loc, loc });
    fn.addEquality({ type, type });
    EXPECT_TRUE(check(fn).malformedExpressions.empty());

    Expr mixed = fn.addEquality({ loc, type });
    EXPECT_EQ(check(fn).malformedExpressions, std::vector<Expr> { mixed });
}

}
