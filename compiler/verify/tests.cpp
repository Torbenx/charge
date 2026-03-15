#include <verify/z3.h>

#include <gtest/gtest.h>

TEST(Verify, Z3) {
    z3::context ctx;
    z3::solver solver(ctx);

    z3::expr x = ctx.bool_const("x");
    z3::expr y = ctx.bool_const("y");
    z3::expr conjecture = (!(x && y)) == (!x || !y);

    solver.add(!conjecture);
    EXPECT_EQ(solver.check(), z3::unsat);
}