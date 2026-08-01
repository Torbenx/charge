#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

static Member newLiteral(Solver& solver) { return solver.newMemberLiteral(); }

TEST(VerifyBackend, MembersDefiningExpression) {
    SolverImpl solver;
    Member l1 = newLiteral(solver);
    Member l2 = newLiteral(solver);
    Member v = solver.newAuxMemberVariable();
    Member composite = solver.composeMembers({ l1, v, l2 });

    auto expression = [&](Member m) {
        auto expr = solver.members.definingExpression(m);
        return std::vector<Member>(expr.begin(), expr.end());
    };

    // Literals and variables are their own expression, the identity is the empty one
    EXPECT_EQ(expression(l1), std::vector<Member> { l1 });
    EXPECT_EQ(expression(v), std::vector<Member> { v });
    EXPECT_TRUE(expression(identity_member).empty());
    EXPECT_EQ(expression(composite), (std::vector<Member> { l1, v, l2 }));

    // The expression is the definition, so it is unaffected by rewrites
    solver.decideTrue(solver.equality(v, l2));
    solver.sat.propagate();
    EXPECT_EQ(solver.members.rewrite(composite), (std::vector<Member> { l1, l2, l2 }));
    EXPECT_EQ(expression(v), std::vector<Member> { v });
    EXPECT_EQ(expression(composite), (std::vector<Member> { l1, v, l2 }));
}

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

namespace {

    //! A set of member expressions to register uses for
    struct UseFixture {
        SolverImpl solver;
        Member l1 = newLiteral(solver);
        Member l2 = newLiteral(solver);
        Member v1 = solver.newAuxMemberVariable();
        Member v2 = solver.newAuxMemberVariable();

        Member compose(std::initializer_list<Member> expression) { return solver.composeMembers(expression); }

        //! Register a use identified by \p id for \p expression
        void addUse(Member expression, uint32_t id) {
            solver.members.addUse(solver, expression, Use(UseKind::Test, id));
        }

        //! The ids the uses notified since the last call were registered with, sorted
        std::vector<uint32_t> takeNotifiedIds() {
            std::vector<uint32_t> result;
            for (Use use : solver.useTest.rewrites) {
                EXPECT_TRUE(use.kind() == UseKind::Test);
                result.push_back(use.id());
            }
            solver.useTest.rewrites.clear();
            std::ranges::sort(result);
            return result;
        }

        void decideEqual(Member a, Member b) {
            solver.decideTrue(solver.equality(a, b));
            solver.sat.propagate();
            solver.members.checkInvariances(solver);
        }

        void backtrack(int_t level) {
            solver.backtrack(level);
            solver.sat.propagate();
            solver.members.checkInvariances(solver);
        }

        //! Start a decision level without touching any of the members
        void newDecisionLevel() {
            solver.decideTrue(solver.newAuxBooleanVariable());
            solver.sat.propagate();
        }
    };

}

TEST(VerifyBackend, MemberUseNotifiedWhenTheNormalFormChanges) {
    UseFixture f;
    f.addUse(f.v1, 1);
    f.addUse(f.v2, 2);

    f.decideEqual(f.v1, f.compose({ f.l1, f.l2 }));

    // Only v1 was rewritten, so the normal form watched by the use of v2 is still v2 itself
    std::vector<Member> expected { f.l1, f.l2 };
    EXPECT_EQ(f.solver.members.rewrite(f.v1), expected);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 1 }));
}

TEST(VerifyBackend, MemberUseNotifiedThroughARewriteExpression) {
    UseFixture f;
    f.addUse(f.v1, 1);

    f.decideEqual(f.v1, f.compose({ f.l1, f.v2 }));
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 1 }));

    // v1 expands through v2, so rewriting v2 changes the normal form of v1 as well. The use is not
    // consumed by the first notification either.
    f.decideEqual(f.v2, f.l2);
    std::vector<Member> expected { f.l1, f.l2 };
    EXPECT_EQ(f.solver.members.rewrite(f.v1), expected);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 1 }));
}

TEST(VerifyBackend, MemberUseOfACompositeExpressionIsNotifiedOnce) {
    UseFixture f;
    f.addUse(f.compose({ f.v1, f.v2 }), 1);

    // Both variables are rewritten to the identity by this, and the use is registered for both of
    // them, so it takes the deduplication to notify it only once
    f.decideEqual(f.compose({ f.v1, f.v2 }), identity_member);
    EXPECT_TRUE(f.solver.members.rewrite(f.v1).empty());
    EXPECT_TRUE(f.solver.members.rewrite(f.v2).empty());
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 1 }));
}

TEST(VerifyBackend, MemberUseOfARepeatedVariableIsNotifiedOnce) {
    UseFixture f;
    f.addUse(f.compose({ f.v1, f.v1 }), 1);

    // The variable is a target of the identity rewrite twice, so it is marked as changed twice
    // while its normal form only changes once
    f.decideEqual(f.compose({ f.v1, f.v1 }), identity_member);
    EXPECT_TRUE(f.solver.members.rewrite(f.v1).empty());
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 1 }));
}

TEST(VerifyBackend, MemberUseOfAnExpressionWithoutVariables) {
    UseFixture f;
    f.addUse(f.compose({ f.l1, f.l2 }), 1);

    // The normal form of a literal expression is the expression itself, so nothing can ever change
    // it and there is nothing to notify
    f.decideEqual(f.v1, f.l1);
    EXPECT_TRUE(f.takeNotifiedIds().empty());
}

TEST(VerifyBackend, MemberUseIsNotNotifiedWhenARewriteIsReverted) {
    UseFixture f;
    f.addUse(f.v1, 1);

    f.decideEqual(f.v1, f.l1);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 1 }));

    // Reverting the rewrite restores v1 as its own normal form, but only growing rewrites are
    // notified about, the use restores itself
    f.backtrack(0);
    std::vector<Member> expected { f.v1 };
    EXPECT_EQ(f.solver.members.rewrite(f.v1), expected);
    EXPECT_TRUE(f.takeNotifiedIds().empty());

    // The use still lives, so reapplying the rewrite notifies it again
    f.decideEqual(f.v1, f.l1);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 1 }));
}

TEST(VerifyBackend, MemberUseIsDiscardedByBacktracking) {
    UseFixture f;
    f.newDecisionLevel();
    f.addUse(f.v1, 1);

    f.backtrack(0);

    // The use was registered at the reverted level, so rewriting v1 does not notify it
    f.decideEqual(f.v1, f.l1);
    EXPECT_TRUE(f.takeNotifiedIds().empty());
}

TEST(VerifyBackend, MemberUseRegisteredForOneVariableOfTheSameExpressionTwice) {
    UseFixture f;
    f.newDecisionLevel();
    f.addUse(f.v1, 1);
    f.addUse(f.v1, 2);

    f.decideEqual(f.v1, f.l1);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 1, 2 }));

    // Reverting the level the uses were registered at has to discard both of them
    f.backtrack(0);
    EXPECT_TRUE(f.takeNotifiedIds().empty());
    f.decideEqual(f.v1, f.l1);
    EXPECT_TRUE(f.takeNotifiedIds().empty());
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