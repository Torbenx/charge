#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

static Member newLiteral(Solver& solver) { return (Member)solver.impl().newValue(TheoryId::MemberLiterals); }

TEST(VerifyBackend, MembersBasic1) {
    SolverImpl solver;
    Member l1 = newLiteral(solver);
    Member l2 = newLiteral(solver);
    Member l3 = newLiteral(solver);
    Member v1 = solver.newAuxMemberVariable();
    Member v2 = solver.newAuxMemberVariable();

    Bool eq1 = solver.equality(v1, solver.composeMembers({ l1, l2 }));
    Bool eq2 = solver.equality(solver.composeMembers({ l1, v2 }), solver.composeMembers({ v1, l3 }));
    Bool eq3 = solver.equality(v2, solver.composeMembers({ l2, l3 }));

    {
        solver.decideTrue(eq1);
        solver.sat.propagate();
        solver.decideTrue(eq2);
        solver.sat.propagate();
        EXPECT_TRUE(solver.assignedTrue(eq3));
        std::vector<Member> expectedV1 { l1, l2 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { l2, l3 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
    {
        solver.backtrack(0);
        std::vector<Member> expectedV1 { v1 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { v2 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
    {
        solver.decideTrue(eq2);
        solver.sat.propagate();
        solver.decideTrue(eq3);
        solver.sat.propagate();
        EXPECT_TRUE(solver.assignedTrue(eq1));
        std::vector<Member> expectedV1 { l1, l2 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { l2, l3 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
    {
        solver.backtrack(0);
        std::vector<Member> expectedV1 { v1 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { v2 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
    {
        solver.decideTrue(eq3);
        solver.sat.propagate();
        solver.decideTrue(eq1);
        solver.sat.propagate();
        EXPECT_TRUE(solver.assignedTrue(eq2));
        std::vector<Member> expectedV1 { l1, l2 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { l2, l3 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
    {
        solver.backtrack(0);
        std::vector<Member> expectedV1 { v1 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { v2 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
}

TEST(VerifyBackend, MembersRewriteUpdate) {
    SolverImpl solver;
    Member l1 = newLiteral(solver);
    Member l2 = newLiteral(solver);
    Member l3 = newLiteral(solver);
    Member v1 = solver.newAuxMemberVariable();
    Member v2 = solver.newAuxMemberVariable();

    Bool eq1 = solver.equality(v1, solver.composeMembers({ l1, v2 }));
    Bool eq2 = solver.equality(v2, solver.composeMembers({ l2, l3 }));

    solver.decideTrue(eq1);
    solver.sat.propagate();
    solver.decideTrue(eq2);
    solver.sat.propagate();
    std::vector<Member> expectedV1 { l1, l2, l3 };
    EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
}

TEST(VerifyBackend, MembersIdentityRewrite) {
    SolverImpl solver;
    Member v1 = solver.newAuxMemberVariable();
    Member v2 = solver.newAuxMemberVariable();
    Member l1 = newLiteral(solver);

    Bool eq0 = solver.equality(solver.composeMembers({ v1, v2 }), l1);
    Bool eq1 = solver.equality(v1, l1);
    Bool eq2 = solver.equality(v2, identity_member);

    solver.decideTrue(eq0);
    solver.sat.propagate();
    solver.decideTrue(eq1);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(eq2));

    solver.backtrack(0);

    solver.decideTrue(eq1);
    solver.sat.propagate();
    solver.decideTrue(eq2);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(eq0));

    solver.backtrack(0);

    solver.decideTrue(eq2);
    solver.sat.propagate();
    solver.decideTrue(eq0);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(eq1));
}

TEST(VerifyBackend, MembersSubExpr) {
    SolverImpl solver;
    Member v1 = solver.newAuxMemberVariable();
    Member v2 = solver.newAuxMemberVariable();
    Member v3 = solver.newAuxMemberVariable();
    Member l1 = newLiteral(solver);

    Bool eq0 = solver.equality(solver.composeMembers({ v1, v2, v3 }), l1);
    Bool eq1 = solver.equality(v1, identity_member);
    Bool eq2 = solver.equality(v2, l1);
    Bool eq3 = solver.equality(v3, identity_member);

    solver.decideTrue(eq0);
    solver.sat.propagate();
    solver.decideTrue(eq2);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(eq1));
    EXPECT_TRUE(solver.assignedTrue(eq3));
}

TEST(VerifyBackend, OutOfOrderRevertedMemberEquality) {
    SolverImpl solver;
    Member v0 = solver.newAuxMemberVariable();
    Member v1 = solver.newAuxMemberVariable();
    Member v2 = solver.newAuxMemberVariable();

    Member l1 = newLiteral(solver);
    Member l2 = newLiteral(solver);
    Member l3 = newLiteral(solver);

    Bool eq1 = solver.equality(v0, solver.composeMembers({ l1, v1 }));
    Bool eq2 = solver.equality(v0, solver.composeMembers({ v2, l2 }));
    Bool eq3 = solver.equality(v0, l3);

    // assign eq1
    solver.decideTrue(eq1);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(eq3));
    {
        // Should be justified by eq1
        auto [clause, index] = solver.sat.justifyAssignment(!eq3);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(index, 0);
        EXPECT_EQ(clause[0], !eq3);
        EXPECT_EQ(clause[1], !eq1);
    }

    // assign eq2
    solver.addClause({ eq2 });
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(eq3));
    {
        // Should still be justified by eq1
        auto [clause, index] = solver.sat.justifyAssignment(!eq3);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(index, 0);
        EXPECT_EQ(clause[0], !eq3);
        EXPECT_EQ(clause[1], !eq1);
    }

    // revert eq1
    auto cachedReason = solver.sat.firstReason(!eq3);
    solver.sat.beginBacktrack(0);
    EXPECT_FALSE(solver.assignedFalse(eq3));
    {
        // Should still be justified by eq1 even after backtrack
        auto [clause, index] = solver.reasonToClause(!eq3, cachedReason);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(index, 0);
        EXPECT_EQ(clause[0], !eq3);
        EXPECT_EQ(clause[1], !eq1);
    }
    solver.sat.endBacktrack();

    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(eq3));
    {
        // After propagating should be justified by eq2
        auto [clause, index] = solver.sat.justifyAssignment(!eq3);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(index, 0);
        EXPECT_EQ(clause[0], !eq3);
        EXPECT_EQ(clause[1], !eq2);
    }
}

}