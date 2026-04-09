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

    BooleanValue eq1 = solver.equality(v1, solver.composeMembers({ l1, l2 }));
    BooleanValue eq2 = solver.equality(solver.composeMembers({ l1, v2 }), solver.composeMembers({ v1, l3 }));
    BooleanValue eq3 = solver.equality(v2, solver.composeMembers({ l2, l3 }));

    println("------------");
    {
        solver.decideTrue(eq1);
        solver.sat.propagate();
        println("-");
        solver.decideTrue(eq2);
        solver.sat.propagate();
        EXPECT_TRUE(solver.assignedTrue(eq3));
        std::vector<Member> expectedV1 { l1, l2 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { l2, l3 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
    println("------------");
    {
        solver.sat.backtrack(0);
        std::vector<Member> expectedV1 { v1 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { v2 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
    println("------------");
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
    println("------------");
    {
        solver.sat.backtrack(0);
        std::vector<Member> expectedV1 { v1 };
        EXPECT_EQ(solver.members.rewrite(v1), expectedV1);
        std::vector<Member> expectedV2 { v2 };
        EXPECT_EQ(solver.members.rewrite(v2), expectedV2);
    }
    println("------------");
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
    println("------------");
    {
        solver.sat.backtrack(0);
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

    BooleanValue eq1 = solver.equality(v1, solver.composeMembers({ l1, v2 }));
    BooleanValue eq2 = solver.equality(v2, solver.composeMembers({ l2, l3 }));

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

    BooleanValue eq0 = solver.equality(solver.composeMembers({ v1, v2 }), l1);
    BooleanValue eq1 = solver.equality(v1, l1);
    BooleanValue eq2 = solver.equality(v2, identity_member);

    solver.decideTrue(eq0);
    solver.sat.propagate();
    solver.decideTrue(eq1);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(eq2));

    solver.sat.backtrack(0);

    solver.decideTrue(eq1);
    solver.sat.propagate();
    solver.decideTrue(eq2);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(eq0));

    solver.sat.backtrack(0);

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

    BooleanValue eq0 = solver.equality(solver.composeMembers({ v1, v2, v3 }), l1);
    BooleanValue eq1 = solver.equality(v1, identity_member);
    BooleanValue eq2 = solver.equality(v2, l1);
    BooleanValue eq3 = solver.equality(v3, identity_member);

    solver.decideTrue(eq0);
    solver.sat.propagate();
    solver.decideTrue(eq2);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(eq1));
    EXPECT_TRUE(solver.assignedTrue(eq3));
}

}