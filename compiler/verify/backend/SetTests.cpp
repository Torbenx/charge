#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, SetsEmptySetBasics) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value emptySet = sets.emptySet();

    BooleanValue emptySetIsEmpty = sets.makeIsEmpty(solver, emptySet);
    EXPECT_TRUE(solver.alwaysTrue(emptySetIsEmpty));

    BooleanValue emptySetEqEmptySet = solver.equality(emptySet, emptySet);
    EXPECT_TRUE(solver.alwaysTrue(emptySetEqEmptySet));

    auto element = sets.newElement(solver);
    solver.sat.propagate();
    EXPECT_TRUE(sets.assignedTrue(solver, element, !Sets::in(emptySet)));
}

TEST(VerifyBackend, SetsEqualityPropagation1) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value a = solver.newAuxUninterpretedConstantSet();
    Value b = solver.newAuxUninterpretedConstantSet();
    Value c = solver.newAuxUninterpretedConstantSet();
    Value ab = sets.union_(solver, { a, b });
    Value ac = sets.union_(solver, { a, c });
    solver.addClause({ solver.equality(b, c) });
    BooleanValue eq = solver.equality(ab, ac);
    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = sets.newElement(solver);
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(ab));
    solver.sat.propagate();
    sets.decideTrue(solver, e, !Sets::in(ac));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.backtrack(0);
    solver.sat.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    sets.decideTrue(solver, e, Sets::in(ac));
    solver.sat.propagate();
    sets.decideTrue(solver, e, !Sets::in(ab));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.backtrack(0);
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsEqualityPropagation2) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value a = solver.newAuxUninterpretedConstantSet();
    Value b = solver.newAuxUninterpretedConstantSet();
    Value c = solver.newAuxUninterpretedConstantSet();
    Value ab = sets.union_(solver, { a, b });
    Value ac = sets.union_(solver, { a, c });
    solver.addClause({ solver.equality(b, c) });
    BooleanValue eq = solver.equality(ab, ac);
    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = sets.newElement(solver);
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { ab }, { ac })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.backtrack(0);
    solver.sat.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { ac }, { ab })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.backtrack(0);
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

}