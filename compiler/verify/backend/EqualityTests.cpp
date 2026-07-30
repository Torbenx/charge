#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, EqualityBasic) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();

    Bool e12 = solver.equality(v1, v2);
    EXPECT_FALSE(solver.assignedEqual(v1, v2));

    solver.decideTrue(e12);
    EXPECT_FALSE(solver.assignedEqual(v1, v2));

    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedEqual(v1, v2));
}

TEST(VerifyBackend, EqualityTreePath2) {
    SolverImpl solver;
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();

    Bool e12 = solver.equality(v1, v2);
    Bool e13 = solver.equality(v1, v3);
    Bool e23 = solver.equality(v2, v3);
    solver.decideTrue(e12);
    solver.sat.propagate();
    solver.decideTrue(e13);
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(e23));
    EXPECT_TRUE(solver.assignedEqual(v2, v3));

    auto [clause, forcedIndex] = solver.sat.justifyAssignment(e23);
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

    Bool e13 = solver.equality(v1, v3);
    Bool e23 = solver.equality(v2, v3);
    Bool e12 = solver.equality(v1, v2);
    solver.decideTrue(e13);
    solver.sat.propagate();
    solver.decideTrue(e23);
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(e12));
    EXPECT_TRUE(solver.assignedEqual(v1, v2));

    auto [clause, forcedIndex] = solver.sat.justifyAssignment(e12);
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
    Bool e00_03 = solver.equality(vals[0][0], vals[0][3]);
    {
        auto [clause, forcedIndex] = solver.sat.justifyAssignment(e00_03);
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
    Bool e30_33 = solver.equality(vals[3][0], vals[3][3]);
    Bool e32_33 = solver.equality(vals[3][2], vals[3][3]);
    EXPECT_FALSE(solver.assignedTrue(e30_33));
    EXPECT_FALSE(solver.assignedTrue(e32_33));
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(e30_33));
    EXPECT_TRUE(solver.assignedTrue(e32_33));

    auto testConnections = [&] {
        {
            auto [clause, forcedIndex] = solver.reasonToClause(e30_33, makeReason(theory_params::eqUninterpretedConstant.equalityReason, {}));
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
            auto [clause, forcedIndex] = solver.reasonToClause(e32_33, makeReason(theory_params::eqUninterpretedConstant.equalityReason, {}));
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

    solver.sat.beginBacktrack(0);
    EXPECT_FALSE(solver.assignedEqual(vals[3][0], vals[3][3]));
    EXPECT_FALSE(solver.assignedEqual(vals[3][2], vals[3][3]));

    testConnections();
    solver.sat.endBacktrack();
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
    Bool c = solver.newAuxBooleanVariable();
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
    Bool c = solver.newAuxBooleanVariable();
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
    Bool c = solver.newAuxBooleanVariable();
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

    Bool e23 = solver.equality(v2, v3);
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

    Bool e13 = solver.equality(v1, v3);
    Bool e23 = solver.equality(v2, v3);

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
    solver.backtrack(solver.currentDecisionLevel());
    solver.sat.propagate();

    // check v2 != v3 still holds
    EXPECT_TRUE(solver.assignedFalse(e13));
    EXPECT_TRUE(solver.assignedFalse(e23));
}

TEST(VerifyBackend, OutOfOrderRevertedDisequalitiyReasons) {
    SolverImpl solver;
    Value v0 = solver.newAuxUninterpretedConstant();
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();
    solver.sat.propagate();

    Bool e01 = solver.equality(v0, v1);
    Bool e02 = solver.equality(v0, v2);
    Bool e03 = solver.equality(v0, v3);
    Bool e13 = solver.equality(v1, v3);
    Bool e23 = solver.equality(v2, v3);

    solver.decideTrue(e01);
    solver.sat.propagate();
    solver.decideTrue(e02);
    solver.sat.propagate();

    // assign v1 != v3
    solver.decideTrue(!e13);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e03));
    {
        auto [clause, index] = solver.sat.justifyAssignment(!e03);
        EXPECT_EQ(clause.size(), 3);
        EXPECT_EQ(clause[0], !e03);
        EXPECT_EQ(clause[1], e13);
        EXPECT_EQ(clause[2], !e01);
    }

    // assign v2 != v3
    solver.addClause({ !e23 });
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e03));
    {
        // Justification unchanged
        auto [clause, index] = solver.sat.justifyAssignment(!e03);
        EXPECT_EQ(clause.size(), 3);
        EXPECT_EQ(clause[0], !e03);
        EXPECT_EQ(clause[1], e13);
        EXPECT_EQ(clause[2], !e01);
    }

    // revert v1 != v3
    auto cachedReason = solver.sat.firstReason(!e03);
    solver.sat.beginBacktrack(solver.currentDecisionLevel());
    {
        // Justification unchanged
        auto [clause, index] = solver.reasonToClause(!e03, cachedReason);
        EXPECT_EQ(clause.size(), 3);
        EXPECT_EQ(clause[0], !e03);
        EXPECT_EQ(clause[1], e13);
        EXPECT_EQ(clause[2], !e01);
    }
    solver.sat.endBacktrack();

    // check v0 != v3 still holds after propagation
    EXPECT_FALSE(solver.assignedFalse(e03));
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e03));
    {
        // Justification updated
        auto [clause, index] = solver.sat.justifyAssignment(!e03);
        EXPECT_EQ(clause.size(), 3);
        EXPECT_EQ(clause[0], !e03);
        EXPECT_EQ(clause[1], e23);
        EXPECT_EQ(clause[2], !e02);
    }
}

TEST(VerifyBackend, OutOfOrderRevertedDisequalitiyReasonsInterleavedTrace) {
    SolverImpl solver;
    Value v0 = solver.newAuxUninterpretedConstant();
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();
    Value v3 = solver.newAuxUninterpretedConstant();

    Bool b0 = solver.newAuxBooleanVariable();
    Bool b1 = solver.newAuxBooleanVariable();

    Bool e01 = solver.equality(v0, v1);
    Bool e02 = solver.equality(v0, v2);
    Bool e03 = solver.equality(v0, v3);
    Bool e13 = solver.equality(v1, v3);
    Bool e23 = solver.equality(v2, v3);

    solver.addClause({ b0, b1, !e13 });
    solver.addClause({ b0, !e23 });
    solver.addClause({ e01 });
    solver.addClause({ e02 });
    solver.sat.propagate();

    solver.decideTrue(!b1);
    solver.sat.propagate();

    // assign v1 != v3 and v2 != v3 at the same time
    // trace order should be v1 != v3, v2 != v3, v0 != v3
    solver.addClause({ !b0 });
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e13));
    EXPECT_TRUE(solver.assignedFalse(e23));
    EXPECT_TRUE(solver.assignedFalse(e03));
    {
        auto [clause, index] = solver.sat.justifyAssignment(!e03);
        EXPECT_EQ(clause.size(), 3);
        EXPECT_EQ(clause[0], !e03);
        EXPECT_EQ(clause[1], e13);
        EXPECT_EQ(clause[2], !e01);
    }

    // revert v1 != v3
    auto cachedReason = solver.sat.firstReason(!e03);
    solver.sat.beginBacktrack(solver.currentDecisionLevel());
    {
        // Justification unchanged
        auto [clause, index] = solver.reasonToClause(!e03, cachedReason);
        EXPECT_EQ(clause.size(), 3);
        EXPECT_EQ(clause[0], !e03);
        EXPECT_EQ(clause[1], e13);
        EXPECT_EQ(clause[2], !e01);
    }
    solver.sat.endBacktrack();

    // check v0 != v3 still holds after propagation
    EXPECT_FALSE(solver.assignedFalse(e03));
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e03));
    {
        // Justification updated
        auto [clause, index] = solver.sat.justifyAssignment(!e03);
        EXPECT_EQ(clause.size(), 3);
        EXPECT_EQ(clause[0], !e03);
        EXPECT_EQ(clause[1], e23);
        EXPECT_EQ(clause[2], !e02);
    }
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

    solver.backtrack(0);
    solver.sat.propagate();

    Bool e12 = solver.equality(v1, v2);
    solver.sat.propagate();
    EXPECT_FALSE(solver.assignedFalse(e12));
}

TEST(VerifyBackend, UseHandle) {
    Use use(UseKind::Test, 7);
    EXPECT_TRUE(use.kind() == UseKind::Test);
    EXPECT_EQ(use.id(), 7u);

    EXPECT_TRUE(use == Use(UseKind::Test, 7));
    EXPECT_FALSE(use == Use(UseKind::Test, 8));

    // The id has to fit in the handle next to the kind
    Use maxUse(UseKind::Test, Use::MAX_ID);
    EXPECT_TRUE(maxUse.kind() == UseKind::Test);
    EXPECT_EQ(maxUse.id(), Use::MAX_ID);
}

TEST(VerifyBackend, UseNotificationDispatch) {
    SolverImpl solver;
    EXPECT_TRUE(solver.useTest.rewrites.empty());

    solver.propagateRewrite(Use(UseKind::Test, 3));
    solver.propagateRewrite(Use(UseKind::Test, 5));

    // The notifications reach the client the kind names, in the order they were made
    EXPECT_EQ(solver.useTest.rewrites.size(), 2u);
    EXPECT_TRUE(solver.useTest.rewrites[0] == Use(UseKind::Test, 3));
    EXPECT_TRUE(solver.useTest.rewrites[1] == Use(UseKind::Test, 5));

    EXPECT_EQ(nameString(UseKind::Test), "Test");
}

namespace {

    //! A set of memory declarations to register uses for
    struct UseFixture {
        SolverImpl solver;
        MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
        MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
        MemoryDeclaration d3 = solver.newAuxMemoryDeclarationVariable();

        UninterpretedEquality& equality() { return solver.memoryDeclarationEquality; }

        //! Register a use identified by \p id for \p value
        void addUse(Value value, uint32_t id) {
            equality().addUse(solver, value, Use(UseKind::Test, id));
        }

        //! The ids the uses notified since the last call were registered with
        std::vector<uint32_t> takeNotifiedIds() {
            std::vector<uint32_t> result;
            for (Use use : solver.useTest.rewrites) {
                EXPECT_TRUE(use.kind() == UseKind::Test);
                result.push_back(use.id());
            }
            solver.useTest.rewrites.clear();
            return result;
        }

        void decideEqual(Value a, Value b) {
            solver.decideTrue(solver.equality(a, b));
            solver.sat.propagate();
            equality().checkInvariances(solver);
        }

        void backtrack(int_t level) {
            solver.backtrack(level);
            solver.sat.propagate();
            equality().checkInvariances(solver);
        }

        //! Start a decision level without touching any of the declarations
        void newDecisionLevel() {
            solver.decideTrue(solver.newAuxBooleanVariable());
            solver.sat.propagate();
        }
    };

}

TEST(VerifyBackend, UseNotifiedWhenRepresentativeChanges) {
    UseFixture f;
    f.addUse(f.d1, 1);
    f.addUse(f.d2, 2);

    f.decideEqual(f.d1, f.d2);

    // d1 stays the representative of the joined class, so only the use of d2 is notified
    EXPECT_TRUE(f.equality().rewrite(f.d2) == (Value)f.d1);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 2 }));
}

TEST(VerifyBackend, UseNotifiedForEveryValueOfTheLinkedClass) {
    UseFixture f;
    f.addUse(f.d1, 1);
    f.addUse(f.d2, 2);
    f.addUse(f.d3, 3);

    // d2 stays the representative here, so the use of d3 is the only one notified
    f.decideEqual(f.d2, f.d3);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 3 }));

    // Now the whole class of d2 is linked below d1, so both of its uses are notified
    f.decideEqual(f.d1, f.d2);
    auto notified = f.takeNotifiedIds();
    std::ranges::sort(notified);
    EXPECT_EQ(notified, std::vector<uint32_t>({ 2, 3 }));
}

TEST(VerifyBackend, UseIsNotConsumedByANotification) {
    UseFixture f;
    f.addUse(f.d3, 3);

    f.decideEqual(f.d2, f.d3);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 3 }));

    // The use still lives, so the next change of the representative notifies it again
    f.decideEqual(f.d1, f.d2);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 3 }));
}

TEST(VerifyBackend, UseIsDiscardedByBacktracking) {
    UseFixture f;
    f.newDecisionLevel();
    f.addUse(f.d2, 2);

    f.backtrack(0);

    // The use was registered at the reverted level, so linking d2 below d1 does not notify it
    f.decideEqual(f.d1, f.d2);
    EXPECT_TRUE(f.takeNotifiedIds().empty());
}

TEST(VerifyBackend, UseSurvivesBacktrackingOfTheLinkThatMovedIt) {
    UseFixture f;
    f.addUse(f.d2, 2);

    f.decideEqual(f.d1, f.d2);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 2 }));

    // Reverting the link hands the use back to d2, so it is notified again when it is relinked
    f.backtrack(0);
    EXPECT_TRUE(f.equality().rewrite(f.d2) == (Value)f.d2);
    f.decideEqual(f.d1, f.d2);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 2 }));
}

TEST(VerifyBackend, UseRegisteredAfterALinkOfTheSameLevel) {
    UseFixture f;
    f.decideEqual(f.d1, f.d2);
    f.takeNotifiedIds();

    // The use goes to the back of d1's list, behind the uses inherited from d2, so reverting the
    // level has to discard it before it puts that tail back into d2
    f.addUse(f.d1, 1);
    f.equality().checkInvariances(f.solver);
    f.backtrack(0);

    f.decideEqual(f.d1, f.d2);
    EXPECT_TRUE(f.takeNotifiedIds().empty());
}

TEST(VerifyBackend, UseRegisteredBeforeALinkOfTheSameLevel) {
    UseFixture f;
    f.newDecisionLevel();
    f.addUse(f.d1, 1);
    f.addUse(f.d2, 2);
    f.decideEqual(f.d1, f.d2);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 2 }));

    // Reverting only the link leaves both uses in place
    f.backtrack(1);
    f.decideEqual(f.d1, f.d2);
    EXPECT_EQ(f.takeNotifiedIds(), std::vector<uint32_t>({ 2 }));

    // Reverting the level they were registered at discards them
    f.backtrack(0);
    f.decideEqual(f.d1, f.d2);
    EXPECT_TRUE(f.takeNotifiedIds().empty());
}

TEST(VerifyBackend, UseOfANonRootValue) {
    UseFixture f;
    f.decideEqual(f.d1, f.d2);

    // The use is registered for a value that is not a root, so it is kept on d1 and notified when
    // the class of d1 is linked somewhere else
    f.addUse(f.d2, 2);
    f.decideEqual(f.d3, f.d1);
    EXPECT_TRUE(f.equality().rewrite(f.d2) == (Value)f.d1);
    EXPECT_TRUE(f.takeNotifiedIds().empty());

    f.backtrack(1);
    f.equality().checkInvariances(f.solver);
}

TEST(VerifyBackend, ExplainEqual) {
    UseFixture f;
    Bool e12 = f.solver.equality(f.d1, f.d2);
    Bool e23 = f.solver.equality(f.d2, f.d3);
    f.decideEqual(f.d1, f.d2);
    f.decideEqual(f.d2, f.d3);

    // The path connecting the two values is justified by the equalities of its links
    auto clause = f.solver.beginClause();
    f.equality().explainEqual(f.solver, f.d1, f.d3, clause);
    auto literals = f.solver.viewClause(clause);
    EXPECT_EQ(literals.size(), 2);
    EXPECT_TRUE(std::find(literals.begin(), literals.end(), !e12) != literals.end());
    EXPECT_TRUE(std::find(literals.begin(), literals.end(), !e23) != literals.end());

    // Nothing has to be justified for a value and itself
    auto sameClause = f.solver.beginClause();
    f.equality().explainEqual(f.solver, f.d1, f.d1, sameClause);
    EXPECT_TRUE(f.solver.viewClause(sameClause).empty());
}

TEST(VerifyBackend, MemoryDeclarationEqualityBasic) {
    SolverImpl solver;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();

    Bool e12 = solver.equality(d1, d2);
    EXPECT_FALSE(solver.assignedEqual(d1, d2));

    solver.decideTrue(e12);
    EXPECT_FALSE(solver.assignedEqual(d1, d2));

    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedEqual(d1, d2));
    solver.memoryDeclarationEquality.checkInvariances(solver);

    solver.backtrack(0);
    solver.sat.propagate();
    EXPECT_FALSE(solver.assignedEqual(d1, d2));
    solver.memoryDeclarationEquality.checkInvariances(solver);
}

TEST(VerifyBackend, MemoryDeclarationEqualityIsIndependentOfConstantEquality) {
    // Both sorts are handled by their own instance of the theory, so the literals of the one must
    // not be confused with those of the other
    SolverImpl solver;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Value v1 = solver.newAuxUninterpretedConstant();
    Value v2 = solver.newAuxUninterpretedConstant();

    Bool declarationEq = solver.equality(d1, d2);
    Bool constantEq = solver.equality(v1, v2);
    EXPECT_FALSE(declarationEq == constantEq);

    solver.decideTrue(declarationEq);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedEqual(d1, d2));
    EXPECT_FALSE(solver.assignedEqual(v1, v2));
    solver.memoryDeclarationEquality.checkInvariances(solver);
    solver.uninterpConstantEquality.checkInvariances(solver);
}

TEST(VerifyBackend, MemoryDeclarationEqualityPath) {
    SolverImpl solver;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d3 = solver.newAuxMemoryDeclarationVariable();

    Bool e12 = solver.equality(d1, d2);
    Bool e13 = solver.equality(d1, d3);
    Bool e23 = solver.equality(d2, d3);
    solver.decideTrue(e12);
    solver.sat.propagate();
    solver.decideTrue(e13);
    solver.sat.propagate();

    // The implied equality is propagated and justified by the two that were decided
    EXPECT_TRUE(solver.assignedTrue(e23));
    EXPECT_TRUE(solver.assignedEqual(d2, d3));
    solver.memoryDeclarationEquality.checkInvariances(solver);

    auto [clause, forcedIndex] = solver.sat.justifyAssignment(e23);
    EXPECT_EQ(clause.size(), 3);
    EXPECT_EQ(clause[forcedIndex], e23);
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e12) != clause.end());
    EXPECT_TRUE(std::find(clause.begin(), clause.end(), !e13) != clause.end());
}

TEST(VerifyBackend, MemoryDeclarationDisequalityPropagation) {
    SolverImpl solver;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d3 = solver.newAuxMemoryDeclarationVariable();

    solver.addClause({ solver.equality(d1, d2) });
    solver.addClause({ !solver.equality(d1, d3) });
    solver.sat.propagate();

    // d1 == d2 and d1 != d3 make d2 and d3 disequal as well
    Bool e23 = solver.equality(d2, d3);
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedFalse(e23));
    solver.memoryDeclarationEquality.checkInvariances(solver);
}

TEST(VerifyBackend, MemoryDeclarationEqualityConflict) {
    SolverImpl solver;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d3 = solver.newAuxMemoryDeclarationVariable();

    solver.addClause({ solver.equality(d1, d2) });
    solver.addClause({ solver.equality(d2, d3) });
    solver.addClause({ !solver.equality(d1, d3) });
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
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
    solver.backtrack(0);

    solver.decideTrue(a);
    EXPECT_TRUE(solver.sat.propagate());
    solver.decideTrue(!b);
    EXPECT_TRUE(solver.sat.propagate());
    EXPECT_TRUE(solver.assignedFalse(eq));
    solver.backtrack(0);

    solver.decideTrue(!a);
    EXPECT_TRUE(solver.sat.propagate());
    solver.decideTrue(b);
    EXPECT_TRUE(solver.sat.propagate());
    EXPECT_TRUE(solver.assignedFalse(eq));
    solver.backtrack(0);

    solver.decideTrue(!a);
    EXPECT_TRUE(solver.sat.propagate());
    solver.decideTrue(!b);
    EXPECT_TRUE(solver.sat.propagate());
    EXPECT_TRUE(solver.assignedTrue(eq));
    solver.backtrack(0);
}

}