#include <types.h>
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

enum class ScalarKind : uint8_t {
    Bool,
    Type,
    MemberExpr,
    MemoryDecl,
};

void setupZ3() {
    z3::context c;

    auto typeSort = c.uninterpreted_sort("type");
    auto mdeclSort = c.uninterpreted_sort("memory-declaration");

    auto memberSort = c.uninterpreted_sort("member-expression");
    auto memberContains = z3::tree_order(memberSort, 0);

    std::array<const char*, 2> mlocTupleNames { "decl", "member" };
    std::array<z3::sort, 2> mlocTupleSorts { mdeclSort, memberSort };
    z3::func_decl_vector mlocProjections(c);
    auto makeLoc = c.tuple_sort("memory-location", 2, mlocTupleNames.data(), mlocTupleSorts.data(), mlocProjections);
    auto mlocSort = makeLoc.range();
    VERIFY(mlocProjections.size() == 2);

    auto mlocDecl = mlocProjections[0];
    VERIFY(mlocDecl.arity() == 1);
    VERIFY(mlocDecl.domain(0).id() == mlocSort.id());
    VERIFY(mlocDecl.range().id() == mdeclSort.id());
    auto mlocMember = mlocProjections[1];
    VERIFY(mlocMember.arity() == 1);
    VERIFY(mlocMember.domain(0).id() == mlocSort.id());
    VERIFY(mlocMember.range().id() == memberSort.id());

    auto mlocContains = c.function("memory-location-contains", mlocSort, mlocSort, c.bool_sort());
    {
        auto parent = c.constant("parent", mlocSort);
        auto child = c.constant("child", mlocSort);
        z3::forall(parent, child, mlocContains(parent, child) == (mlocDecl(parent) == mlocDecl(child) && memberContains(parent, child)));
    }


}