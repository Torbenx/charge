#include <verify/backend/DecisionDriver.h>
#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

namespace {

    struct Fixture {
        SolverImpl solver;

        Bool newVariable() { return solver.newAuxBooleanVariable(); }
        void addClause(std::vector<Bool> clause) { solver.addClause(std::move(clause)); }

        GrindResult grind() { return solver.grindDecisions(); }

        //! Make the assumption that \p lit holds, which the search below will not take back
        void assume(Bool lit) {
            // A decision is made on an empty propagation queue, so whatever stating the problem
            // or the previous assumption assigned is propagated first
            solver.propagate();
            solver.decideTrue(lit);
        }

        bool assignedTrue(Bool lit) { return solver.assignedTrue(lit); }
        //! Whether \p variable holds either polarity, which every variable of a model does
        bool assigned(Bool variable) {
            return solver.assignedTrue(variable) || solver.assignedFalse(variable);
        }
    };

    //! State that each of \p pigeons pigeons sits in one of \p holes holes without sharing one
    /*!
    The problem has a model exactly when there are at least as many holes as pigeons. With one hole
    less, resolution needs exponentially many steps to refute it, which is what makes it a workout
    for the conflict driven search.
    */
    std::vector<std::vector<Bool>> statePigeonhole(Fixture& f, int_t pigeons, int_t holes) {
        std::vector<std::vector<Bool>> inHole;
        for (int_t pigeon = 0; pigeon < pigeons; pigeon++) {
            std::vector<Bool> row;
            for (int_t hole = 0; hole < holes; hole++)
                row.push_back(f.newVariable());
            f.addClause(row);
            inHole.push_back(std::move(row));
        }

        for (int_t hole = 0; hole < holes; hole++) {
            for (int_t first = 0; first < pigeons; first++) {
                for (int_t second = first + 1; second < pigeons; second++)
                    f.addClause({ !inHole[first][hole], !inHole[second][hole] });
            }
        }
        return inHole;
    }

}

TEST(VerifyBackend, DecisionModel) {
    Fixture f;
    Bool a = f.newVariable();
    Bool b = f.newVariable();
    Bool c = f.newVariable();
    f.addClause({ a, b });
    f.addClause({ !a, c });
    f.addClause({ !b, !c });

    EXPECT_EQ(f.grind(), GrindResult::Model);

    // A model assigns every variable and satisfies every clause
    EXPECT_TRUE(f.assigned(a));
    EXPECT_TRUE(f.assigned(b));
    EXPECT_TRUE(f.assigned(c));
    EXPECT_TRUE(f.assignedTrue(a) || f.assignedTrue(b));
    EXPECT_TRUE(f.assignedTrue(!a) || f.assignedTrue(c));
    EXPECT_TRUE(f.assignedTrue(!b) || f.assignedTrue(!c));
    EXPECT_TRUE(f.solver.checkAssignment());
}

TEST(VerifyBackend, DecisionUnsatisfiable) {
    Fixture f;
    Bool a = f.newVariable();
    Bool b = f.newVariable();
    // The four clauses rule out each of the four ways to assign the two variables
    f.addClause({ a, b });
    f.addClause({ a, !b });
    f.addClause({ !a, b });
    f.addClause({ !a, !b });

    EXPECT_EQ(f.grind(), GrindResult::UnconditionallyUnsatisfiable);
}

TEST(VerifyBackend, DecisionUnsatisfiableAfterDecisions) {
    Fixture f;
    Bool a = f.newVariable();
    Bool b = f.newVariable();
    Bool c = f.newVariable();
    f.addClause({ a, b });
    f.addClause({ a, !b });
    f.addClause({ !a, b });
    f.addClause({ !a, !b });

    // A problem without a model has none under assumptions either, and saying so is the stronger
    // statement, so it takes precedence over the assumptions being refuted
    f.assume(c);
    EXPECT_EQ(f.grind(), GrindResult::UnconditionallyUnsatisfiable);
}

TEST(VerifyBackend, DecisionAssumptionsUnsatisfiable) {
    Fixture f;
    Bool a = f.newVariable();
    Bool b = f.newVariable();
    Bool c = f.newVariable();
    // Holding both assumptions forces 'c' to be true and to be false, which no propagation of a
    // single one of them sees
    f.addClause({ !a, !b, c });
    f.addClause({ !a, !b, !c });

    f.assume(a);
    f.assume(b);
    EXPECT_EQ(f.solver.currentDecisionLevel(), 1);

    // The two assumptions cannot hold at the same time
    EXPECT_EQ(f.grind(), GrindResult::AssumptionsUnsatisfiable);
    EXPECT_LT(f.solver.currentDecisionLevel(), 1);

    // Dropping the assumptions leaves a problem that does have a model
    f.solver.backtrack(0);
    EXPECT_EQ(f.grind(), GrindResult::Model);
    EXPECT_TRUE(f.solver.checkAssignment());
}

TEST(VerifyBackend, DecisionKeepsAssumptions) {
    Fixture f;
    Bool a = f.newVariable();
    Bool b = f.newVariable();
    f.addClause({ !a, b });

    // Assumptions that a model can be found under are still assigned by the time it is
    f.assume(a);
    EXPECT_EQ(f.grind(), GrindResult::Model);
    EXPECT_TRUE(f.assignedTrue(a));
    EXPECT_TRUE(f.assignedTrue(b));
    EXPECT_GE(f.solver.currentDecisionLevel(), 0);
}

TEST(VerifyBackend, DecisionRestarts) {
    Fixture f;
    statePigeonhole(f, 7, 6);

    EXPECT_EQ(f.grind(), GrindResult::UnconditionallyUnsatisfiable);
    // The refutation takes far more conflicts than the shortest restart interval holds
    EXPECT_GT(f.solver.decisionDriver.restartCount(), 0u);
}

TEST(VerifyBackend, DecisionSatisfiablePigeonhole) {
    Fixture f;
    // With as many holes as pigeons every pigeon finds one of its own
    auto inHole = statePigeonhole(f, 5, 5);

    EXPECT_EQ(f.grind(), GrindResult::Model);
    EXPECT_TRUE(f.solver.checkAssignment());

    for (const auto& row : inHole) {
        int_t occupied = 0;
        for (Bool lit : row)
            occupied += f.assignedTrue(lit) ? 1 : 0;
        EXPECT_GE(occupied, 1);
    }
}

TEST(VerifyBackend, DecisionEqualityModel) {
    Fixture f;
    Value a = f.solver.newAuxUninterpretedConstant();
    Value b = f.solver.newAuxUninterpretedConstant();
    Value c = f.solver.newAuxUninterpretedConstant();
    Bool ab = f.solver.equality(a, b);
    Bool ac = f.solver.equality(a, c);
    Bool bc = f.solver.equality(b, c);
    f.addClause({ ab, ac });
    f.addClause({ !bc });

    // The equality literals are variables of the problem just like the boolean ones, and the
    // theory keeps the model of them consistent: 'a = b' and 'a = c' cannot both be picked
    EXPECT_EQ(f.grind(), GrindResult::Model);
    EXPECT_TRUE(f.assignedTrue(ab) || f.assignedTrue(ac));
    EXPECT_FALSE(f.assignedTrue(ab) && f.assignedTrue(ac));
    EXPECT_TRUE(f.solver.checkAssignment());
}

TEST(VerifyBackend, DecisionSetContainmentModel) {
    Fixture f;
    SetElement element = f.solver.newSetElement(Sort::UninterpretedConstantSet);
    Set a = f.solver.newAuxUninterpretedConstantSet();
    Set b = f.solver.newAuxUninterpretedConstantSet();
    Bool inA = f.solver.mapToBool(element, Sets::in(a));
    Bool inB = f.solver.mapToBool(element, Sets::in(b));
    // Every clause of the set theory holds a negative containment, so the membership in the one
    // set is stated as the assumption below instead of as a clause
    f.addClause({ !inA, inB });

    // The containment literals are variables of the problem and are decided like the others
    f.assume(inA);
    EXPECT_EQ(f.grind(), GrindResult::Model);
    EXPECT_TRUE(f.solver.assignedTrue(element, Sets::in(a)));
    EXPECT_TRUE(f.solver.assignedTrue(element, Sets::in(b)));
    EXPECT_TRUE(f.solver.checkAssignment());
}

}
