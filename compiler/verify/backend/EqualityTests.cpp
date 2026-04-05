#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

static ClauseAndIndex equalityClause(SolverImpl& solver, BooleanValue lit) {
    return solver.reasonToClause(lit, makeReason<ReasonKind::Equality>({}));
}

TEST(VerifyBackend, EqualityTreePath1) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();

    BooleanValue e12 = solver.equality(v1, v2);
    EXPECT_FALSE(solver.assignedEqual(v1, v2));

    solver.decideTrue(e12);
    EXPECT_FALSE(solver.assignedEqual(v1, v2));

    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedEqual(v1, v2));

    {
        auto [clause, forcedIndex] = equalityClause(solver, e12);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(forcedIndex, 0);
        EXPECT_EQ(clause[0], e12);
        EXPECT_EQ(clause[1], !e12);
    }

    solver.sat.backtrack(0);
    {
        auto [clause, forcedIndex] = equalityClause(solver, e12);
        EXPECT_EQ(clause.size(), 2);
        EXPECT_EQ(forcedIndex, 0);
        EXPECT_EQ(clause[0], e12);
        EXPECT_EQ(clause[1], !e12);
    }
}

TEST(VerifyBackend, EqualityTreePath2) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();

    BooleanValue e12 = solver.equality(v1, v2);
    BooleanValue e13 = solver.equality(v1, v3);
    BooleanValue e23 = solver.equality(v2, v3);
    solver.decideTrue(e12);
    solver.sat.propagate();
    solver.decideTrue(e13);
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(e23));
    EXPECT_TRUE(solver.assignedEqual(v2, v3));

    auto [clause, forcedIndex] = equalityClause(solver, e23);
    EXPECT_EQ(clause.size(), 3);
    EXPECT_EQ(clause[forcedIndex], e23);
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e12) != clause.end());
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e13) != clause.end());
}

TEST(VerifyBackend, EqualityTreePath3) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();

    BooleanValue e13 = solver.equality(v1, v3);
    BooleanValue e23 = solver.equality(v2, v3);
    BooleanValue e12 = solver.equality(v1, v2);
    solver.decideTrue(e13);
    solver.sat.propagate();
    solver.decideTrue(e23);
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(e12));
    EXPECT_TRUE(solver.assignedEqual(v1, v2));

    auto [clause, forcedIndex] = equalityClause(solver, e12);
    EXPECT_EQ(clause.size(), 3);
    EXPECT_EQ(clause[forcedIndex], e12);
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e13) != clause.end());
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e23) != clause.end());
}

TEST(VerifyBackend, EqualityTreePath4) {
    SolverImpl solver;
    std::vector<std::vector<Value>> vals;
    for (int_t i = 0; i < 4; i++) {
        vals.emplace_back();
        for (int_t j = 0; j < 4; j++)
            vals.back().push_back(solver.newAuxUninterpretedConstant());
    }

    solver.decideTrue(solver.equality(vals[0][0], vals[0][1]));
    solver.sat.propagate();
    solver.decideTrue(solver.equality(vals[0][1], vals[0][2]));
    solver.sat.propagate();
    solver.decideTrue(solver.equality(vals[0][2], vals[0][3]));
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedEqual(vals[0][0], vals[0][3]));
    BooleanValue e00_03 = solver.equality(vals[0][0], vals[0][3]);
    {
        auto [clause, forcedIndex] = equalityClause(solver, e00_03);
        EXPECT_EQ(clause.size(), 4);
        EXPECT_EQ(clause[forcedIndex], solver.equality(vals[0][0], vals[0][3]));
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][0], vals[0][1])) != clause.end());
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][1], vals[0][2])) != clause.end());
        EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][2], vals[0][3])) != clause.end());
    }
    solver.sat.propagate();

    for (int_t i = 0; i < 4; i++) {
        solver.decideTrue(solver.equality(vals[2][i], vals[3][i]));
        solver.sat.propagate();
        solver.decideTrue(solver.equality(vals[1][i], vals[2][i]));
        solver.sat.propagate();
        solver.decideTrue(solver.equality(vals[0][i], vals[1][i]));
        solver.sat.propagate();
    }

    EXPECT_TRUE(solver.assignedEqual(vals[3][0], vals[3][3]));
    EXPECT_TRUE(solver.assignedEqual(vals[3][2], vals[3][3]));
    BooleanValue e30_33 = solver.equality(vals[3][0], vals[3][3]);
    BooleanValue e32_33 = solver.equality(vals[3][2], vals[3][3]);
    EXPECT_FALSE(solver.assignedTrue(e30_33));
    EXPECT_FALSE(solver.assignedTrue(e32_33));
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(e30_33));
    EXPECT_TRUE(solver.assignedTrue(e32_33));

    auto testConnections = [&] {
        {
            auto [clause, forcedIndex] = equalityClause(solver, e30_33);
            EXPECT_EQ(clause.size(), 10);
            EXPECT_EQ(clause[forcedIndex], solver.equality(vals[3][0], vals[3][3]));

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][0], vals[1][0])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[1][0], vals[2][0])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[2][0], vals[3][0])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][0], vals[0][1])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][1], vals[0][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][2], vals[0][3])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][3], vals[1][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[1][3], vals[2][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[2][3], vals[3][3])) != clause.end());
        }

        {
            auto [clause, forcedIndex] = equalityClause(solver, e32_33);
            EXPECT_EQ(clause.size(), 8);
            EXPECT_EQ(clause[forcedIndex], solver.equality(vals[3][2], vals[3][3]));

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][2], vals[1][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[1][2], vals[2][2])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[2][2], vals[3][2])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][2], vals[0][3])) != clause.end());

            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[0][3], vals[1][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[1][3], vals[2][3])) != clause.end());
            EXPECT_TRUE(std::find(clause.begin(), clause.end(), !solver.equality(vals[2][3], vals[3][3])) != clause.end());
        }
    };
    testConnections();

    solver.sat.backtrack(0);
    EXPECT_FALSE(solver.assignedEqual(vals[3][0], vals[3][3]));
    EXPECT_FALSE(solver.assignedEqual(vals[3][2], vals[3][3]));

    testConnections();
}

TEST(VerifyBackend, EqualityPropagation1) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();
    solver.addClause({ solver.equality(v1, v2) });
    solver.addClause({ solver.equality(v2, v3) });
    solver.addClause({ !solver.equality(v1, v3) });
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
}

TEST(VerifyBackend, EqualityPropagation2) {
    SolverImpl solver;
    BooleanValue c = solver.newAuxBooleanVariable();
    Value s = solver.newAuxUninterpretedConstant();
    Value t1 = solver.newAuxUninterpretedConstant();
    Value t2 = solver.newAuxUninterpretedConstant();
    solver.addClause({ c, solver.equality(s, t1), solver.equality(s, t2) });
    solver.addClause({ !c });
    solver.addClause({ solver.equality(t1, t2) });
    solver.addClause({ !solver.equality(s, t1) });
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
}

TEST(VerifyBackend, DisequalityPropagation1) {
    SolverImpl solver;
    BooleanValue c = solver.newAuxBooleanVariable();
    Value s = solver.newAuxUninterpretedConstant();
    Value t1 = solver.newAuxUninterpretedConstant();
    Value t2 = solver.newAuxUninterpretedConstant();
    solver.addClause({ c, solver.equality(s, t1), solver.equality(s, t2) });
    solver.addClause({ !solver.equality(s, t1) });
    solver.addClause({ solver.equality(t1, t2) });
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(c));
}

TEST(VerifyBackend, DisequalityPropagation2) {
    SolverImpl solver;
    BooleanValue c = solver.newAuxBooleanVariable();
    Value s = solver.newAuxUninterpretedConstant();
    Value t1 = solver.newAuxUninterpretedConstant();
    Value t2 = solver.newAuxUninterpretedConstant();
    solver.addClause({ c, solver.equality(s, t1), solver.equality(s, t2) });
    solver.addClause({ solver.equality(t1, t2) });
    solver.addClause({ !solver.equality(s, t1) });
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(c));
}

TEST(VerifyBackend, EqualityProblem) {
    SolverImpl solver;
    Value s = solver.newAuxUninterpretedConstant();
    Value t1 = solver.newAuxUninterpretedConstant();
    Value t2 = solver.newAuxUninterpretedConstant();
    Value t3 = solver.newAuxUninterpretedConstant();

    solver.addClause({ !solver.equality(s, t1), !solver.equality(s, t2), solver.equality(s, t3) });
    solver.addClause({ !solver.equality(s, t1), solver.equality(s, t2), !solver.equality(s, t3) });
    solver.addClause({ solver.equality(s, t1), !solver.equality(s, t2), !solver.equality(s, t3) });

    solver.addClause({ !solver.equality(t1, t2), !solver.equality(t1, t3) });
    solver.addClause({ solver.equality(t1, t2), solver.equality(t1, t3), solver.equality(t2, t3) });

    solver.addClause({ !solver.equality(t1, t2), solver.equality(s, t1), solver.equality(s, t2) });
    solver.addClause({ !solver.equality(t1, t3), solver.equality(s, t1), solver.equality(s, t3) });
    solver.addClause({ !solver.equality(t2, t3), solver.equality(s, t2), solver.equality(s, t3) });

    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    solver.decideTrue(solver.equality(s, t1));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    solver.decideTrue(solver.equality(s, t2));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());

    EXPECT_TRUE(solver.sat.analyzeConflicts());
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());

    EXPECT_TRUE(solver.sat.analyzeConflicts());
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    solver.decideTrue(solver.equality(s, t2));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());

    EXPECT_TRUE(solver.sat.analyzeConflicts());
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    solver.decideTrue(solver.equality(s, t3));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());

    EXPECT_TRUE(solver.sat.analyzeConflicts());
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());

    EXPECT_FALSE(solver.sat.analyzeConflicts());
}

TEST(VerifyBackend, DisequalityOfParentAppliesToNewEdgeAddedOnChild) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();
    solver.sat.propagate();

    solver.decideTrue(solver.equality(v1, v2));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    solver.decideTrue(!solver.equality(v1, v3));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    BooleanValue e23 = solver.equality(v2, v3);
    EXPECT_TRUE(solver.infoFor(!e23).tentativelyTrue());
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e23));
}

TEST(VerifyBackend, OutOfOrderRevertedDisequalities) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();
    solver.sat.propagate();

    BooleanValue e13 = solver.equality(v1, v3);
    BooleanValue e23 = solver.equality(v2, v3);

    solver.decideTrue(solver.equality(v1, v2));
    solver.sat.propagate();

    // assign v1 != v3
    solver.decideTrue(!e13);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e13));
    EXPECT_TRUE(solver.assignedFalse(e23));

    // assign v2 != v3
    solver.addClause({ !e23 });
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e13));
    EXPECT_TRUE(solver.assignedFalse(e23));

    // revert v1 != v3
    solver.sat.backtrack(solver.currentDecisionLevel());
    solver.sat.propagate();

    // check v2 != v3 still holds
    EXPECT_TRUE(solver.assignedFalse(e13));
    EXPECT_TRUE(solver.assignedFalse(e23));
}

TEST(VerifyBackend, DisequalityCleanedUpInParents) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();
    Value v4 = solver.newAuxUninterpretedConstant();
    solver.sat.propagate();

    solver.decideTrue(!solver.equality(v3, v4));
    solver.sat.propagate();

    solver.decideTrue(solver.equality(v1, v3));
    solver.sat.propagate();

    solver.decideTrue(solver.equality(v2, v4));
    solver.sat.propagate();

    solver.sat.backtrack(0);
    solver.sat.propagate();

    BooleanValue e12 = solver.equality(v1, v2);
    solver.sat.propagate();
    EXPECT_FALSE(solver.assignedFalse(e12));
}

TEST(VerifyBackend, BooleanEqual) {
    SolverImpl solver;
    auto a = solver.newAuxBooleanVariable();
    auto b = solver.newAuxBooleanVariable();
    EXPECT_EQ(a, solver.equality(a, true_literal));
    EXPECT_EQ(!a, solver.equality(!a, true_literal));
    EXPECT_EQ(!a, solver.equality(a, false_literal));
    EXPECT_EQ(a, solver.equality(!a, false_literal));

    auto eq = solver.equality(a, b);
    EXPECT_FALSE(solver.assignedTrue(eq));
    EXPECT_FALSE(solver.assignedTrue(eq));

    // Test truth table
    solver.decideTrue(a);
    EXPECT_TRUE(solver.sat.propagate());
    solver.decideTrue(b);
    EXPECT_TRUE(solver.sat.propagate());
    EXPECT_TRUE(solver.assignedTrue(eq));
    solver.sat.backtrack(0);

    solver.decideTrue(a);
    EXPECT_TRUE(solver.sat.propagate());
    solver.decideTrue(!b);
    EXPECT_TRUE(solver.sat.propagate());
    EXPECT_TRUE(solver.assignedFalse(eq));
    solver.sat.backtrack(0);

    solver.decideTrue(!a);
    EXPECT_TRUE(solver.sat.propagate());
    solver.decideTrue(b);
    EXPECT_TRUE(solver.sat.propagate());
    EXPECT_TRUE(solver.assignedFalse(eq));
    solver.sat.backtrack(0);

    solver.decideTrue(!a);
    EXPECT_TRUE(solver.sat.propagate());
    solver.decideTrue(!b);
    EXPECT_TRUE(solver.sat.propagate());
    EXPECT_TRUE(solver.assignedTrue(eq));
    solver.sat.backtrack(0);
}

}