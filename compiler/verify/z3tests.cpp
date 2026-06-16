#include <types.h>
#include <verify/z3.h>

#include <gtest/gtest.h>

#include <array>
#include <utility>

TEST(Verify, Z3) {
    z3::context ctx;
    z3::solver solver(ctx);

    z3::expr x = ctx.bool_const("x");
    z3::expr y = ctx.bool_const("y");
    z3::expr conjecture = (!(x && y)) == (!x || !y);

    solver.add(!conjecture);
    EXPECT_EQ(solver.check(), z3::unsat);
}

z3::expr z3forall(std::initializer_list<z3::expr> quant, z3::expr pred) {
    z3::expr_vector quantVec(pred.ctx());
    for (const auto& q : quant)
        quantVec.push_back(q);
    return z3::forall(quantVec, pred);
}

z3::expr z3forall(std::initializer_list<z3::expr> quant, z3::expr pred, std::initializer_list<z3::expr> multiPattern) {
    std::vector<Z3_app> quantVec;
    for (const auto& q : quant)
        quantVec.push_back(q);
    std::vector<Z3_ast> patternVec;
    for (const auto& p : multiPattern)
        patternVec.push_back(p);
    Z3_pattern patternHandle = Z3_mk_pattern(pred.ctx(), multiPattern.size(), patternVec.data());
    pred.check_error();
    auto r = Z3_mk_forall_const(pred.ctx(), 0, quant.size(), quantVec.data(), 1, &patternHandle, pred);
    pred.check_error();
    return { pred.ctx(), r };
}

z3::expr z3forall(std::initializer_list<z3::expr> quant, z3::expr pred, z3::expr pattern) {
    return z3forall(quant, pred, { pattern });
}

TEST(Verify, Z3member) {
    z3::context c;
    z3::solver s = { c, Z3_mk_simple_solver(c) };
    // s.set("smt.mbqi", false);

    auto typeSort = c.uninterpreted_sort("Type");
    auto mdeclSort = c.uninterpreted_sort("MemoryDecl");

    auto memberSort = c.uninterpreted_sort("Member");
    auto memberCompose = z3::function("MemberCompose", memberSort, memberSort, memberSort);
    {
        // Remove common prefix
        auto p = c.constant("parent", memberSort);
        auto c1 = c.constant("child1", memberSort);
        auto c2 = c.constant("child2", memberSort);
        auto pattern = memberCompose(p, c1) == memberCompose(p, c2);
        s.add(z3forall({ p, c1, c2 }, pattern == (c1 == c2), pattern));
    }
    {
        // Associative law
        auto m1 = c.constant("member1", memberSort);
        auto m2 = c.constant("member2", memberSort);
        auto m3 = c.constant("member3", memberSort);
        auto pattern = memberCompose(memberCompose(m1, m2), m3);
        s.add(z3forall({ m1, m2, m3 }, pattern == memberCompose(m1, memberCompose(m2, m3)), pattern));
    }

    {
        auto p1 = c.constant("parent1", memberSort);
        auto p2 = c.constant("parent2", memberSort);
        auto c1 = c.constant("child1", memberSort);
        auto c2 = c.constant("child2", memberSort);
        auto i1 = c.constant("intermediate1", memberSort);
        auto i2 = c.constant("intermediate2", memberSort);
        auto pattern = memberCompose(p1, c1) == memberCompose(p2, c2);
        auto case1 = z3::exists(i1, memberCompose(p1, i1) == p2); // && c1 == memberCompose(leftExt(c1, c2), c2);
        auto case2 = z3::exists(i2, memberCompose(p2, i2) == p1); // && c2 == memberCompose(leftExt(c2, c1), c1);
        s.add(z3forall({ p1, p2, c1, c2 }, z3::implies(pattern, case1 || case2), pattern));
    }

    {
        auto m1 = c.constant("member1", memberSort);
        auto i = c.constant("intermediate", memberSort);
        s.add(z3forall({ m1 }, z3::exists(i, memberCompose(m1, i) == m1)));
    }

    auto isId = z3::function("isidentity", memberSort, c.bool_sort());
    {
        auto m1 = c.constant("member1", memberSort);
        auto m2 = c.constant("member2", memberSort);
        auto pattern = memberCompose(m1, m2) == m1;
        s.add(z3forall({ m1, m2 }, pattern == isId(m2), pattern));
        s.add(z3forall({ m1, m2 }, isId(m2) == pattern, { memberCompose(m1, m2), isId(m2) }));
    }

    {
        auto m1 = c.constant("member1", memberSort);
        auto m2 = c.constant("member2", memberSort);
        auto pattern = isId(memberCompose(m1, m2));
        s.add(z3forall({ m1, m2 }, pattern == (isId(m1) && isId(m2))), pattern);
    }

    auto memberContains = z3::function("MemberContains", memberSort, memberSort, c.bool_sort());
    {
        auto m1 = c.constant("member1", memberSort);
        auto m2 = c.constant("member2", memberSort);
        auto i = c.constant("intermediate", memberSort);
        auto pattern = memberContains(m1, m2);
        s.add(z3forall({ m1, m2 }, pattern == z3::exists(i, m2 == memberCompose(m1, i)), pattern));
    }

    auto m1 = c.constant("m1", memberSort);
    auto m2 = c.constant("m2", memberSort);
    auto m3 = c.constant("m3", memberSort);
    auto m4 = c.constant("m4", memberSort);

    dbgln("reflexive:");
    s.push();
    s.add(!memberContains(m1, m1));
    EXPECT_EQ(s.check(), z3::unsat);
    s.pop();

    dbgln("antisymmetric:");
    s.push();
    s.add(memberContains(m1, m2) && memberContains(m2, m1));
    s.add(m1 != m2);
    EXPECT_EQ(s.check(), z3::unsat);
    s.pop();

    dbgln("transitive:");
    s.push();
    s.add(memberContains(m1, m2) && memberContains(m2, m3));
    s.add(!memberContains(m1, m3));
    EXPECT_EQ(s.check(), z3::unsat);
    s.pop();

    dbgln("tree:");
    s.push();
    s.add(memberContains(m1, m3) && memberContains(m2, m3));
    s.add(!memberContains(m1, m2) && !memberContains(m2, m1));
    EXPECT_EQ(s.check(), z3::unsat);
    s.pop();

    dbgln("test:");
    s.add(memberContains(memberCompose(memberCompose(m1, m2), m3), memberCompose(memberCompose(m1, m2), m4)));
    // s.add(!memberContains(memberCompose(m1, memberCompose(m2, m3)), memberCompose(m1, memberCompose(m2, m4))));
    auto q = c.constant("q", memberSort);
    // s.add(z3forall({ q }, memberCompose(memberCompose(m1, memberCompose(m2, m3)), q) != memberCompose(m1, memberCompose(m2, m4))));
    // s.add(z3forall({ q }, memberCompose(m1, memberCompose(m2, memberCompose(m3, q))) != memberCompose(m1, memberCompose(m2, m4))));
    // s.add(z3forall({ q }, memberCompose(m2, memberCompose(m3, q)) != memberCompose(m2, m4)));
    // s.add(z3forall({ q }, memberCompose(m3, q) != m4));
    s.add(!memberContains(m3, m4));
    EXPECT_EQ(s.check(), z3::unsat);
}