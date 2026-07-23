#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, SetsEmptySetBasics) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value emptySet = sets.emptySet();

    BooleanValue emptySetIsEmpty = sets.isEmpty(solver, emptySet);
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

TEST(VerifyBackend, SetsUnionInterDistribution) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value a = solver.newAuxUninterpretedConstantSet();
    Value b = solver.newAuxUninterpretedConstantSet();
    Value c = solver.newAuxUninterpretedConstantSet();

    Value ibc = sets.intersection(solver, { b, c });
    Value lhs = sets.union_(solver, { a, ibc });
    Value uab = sets.union_(solver, { a, b });
    Value uac = sets.union_(solver, { a, c });
    Value rhs = sets.intersection(solver, { uab, uac });
    BooleanValue eq = solver.equality(lhs, rhs);

    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = sets.newElement(solver);
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { rhs }, { lhs })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.backtrack(0);
    solver.sat.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { lhs }, { rhs })));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());
    sets.decideTrue(solver, e, Sets::in(a));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.backtrack(0);
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsProveDisequal) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value a = solver.newAuxUninterpretedConstantSet();
    Value b = solver.newAuxUninterpretedConstantSet();
    Value c = solver.newAuxUninterpretedConstantSet();

    solver.addClause({ sets.isEmpty(solver, sets.intersection(solver, { a, c })) });
    solver.addClause({ sets.isEmpty(solver, sets.intersection(solver, { b, c })) });
    solver.addClause({ !sets.isEmpty(solver, c) });
    BooleanValue eq = solver.equality(sets.union_(solver, { a, c }), b);
    solver.sat.propagate();

    EXPECT_FALSE(solver.assignedFalse(eq));
    solver.decideTrue(eq);
    EXPECT_FALSE(solver.sat.hasConflicts());

    auto e = sets.newElement(solver);
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(c));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();

    EXPECT_EQ(solver.currentDecisionLevel(), -1);
    EXPECT_TRUE(solver.assignedFalse(eq));
}

TEST(VerifyBackend, SetsUnionOfEqualSets) {
    // Prove that union(A, B) = A if A = B
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value a = solver.newAuxUninterpretedConstantSet();
    Value b = solver.newAuxUninterpretedConstantSet();
    Value u = sets.union_(solver, { a, b });
    BooleanValue eq = solver.equality(u, a);

    auto e = sets.newElement(solver);
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { a }, { u })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();

    solver.decideTrue(solver.equality(a, b));
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { u }, { a })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsSingletons) {
    // Prove that {a}u{b} = {a} iff a = b
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value aValue = solver.newAuxUninterpretedConstant();
    Value bValue = solver.newAuxUninterpretedConstant();

    Value a = solver.uninterpConstantSingletons.singleton(solver, aValue);
    Value b = solver.uninterpConstantSingletons.singleton(solver, bValue);
    Value u = sets.union_(solver, { a, b });
    BooleanValue valueEq = solver.equality(aValue, bValue);
    BooleanValue singletonEq = solver.equality(a, b);
    BooleanValue eq = solver.equality(u, a);
    solver.sat.propagate();

    auto e = sets.newElement(solver);
    solver.sat.propagate();

    // 1. a = b implies {a}u{b} = {a}
    solver.decideTrue(valueEq);
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { a }, { u })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { u }, { a })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
    solver.backtrack(0);
    solver.sat.propagate();

    // 2. {a}u{b} = {a} implies a = b
    solver.decideTrue(eq);
    solver.sat.propagate();
    solver.decideTrue(!valueEq);
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(b));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(valueEq));

}

}