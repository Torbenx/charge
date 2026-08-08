#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, SetsEmptySetBasics) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set emptySet = sets.emptySet();

    Bool emptySetIsEmpty = sets.isEmpty(solver, emptySet);
    EXPECT_TRUE(solver.alwaysTrue(emptySetIsEmpty));

    Bool emptySetEqEmptySet = solver.equality(emptySet, emptySet);
    EXPECT_TRUE(solver.alwaysTrue(emptySetEqEmptySet));

    auto element = sets.newElement(solver);
    solver.propagate();
    EXPECT_TRUE(sets.assignedTrue(solver, element, !Sets::in(emptySet)));
}

TEST(VerifyBackend, SetsContainedElementWitnessesNonEmpty) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    auto e = sets.newElement(solver);
    solver.propagate();

    Bool empty = sets.isEmpty(solver, a);
    EXPECT_FALSE(solver.assignedTrue(empty));
    EXPECT_FALSE(solver.assignedFalse(empty));

    int_t level = solver.currentDecisionLevel();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(solver.assignedFalse(empty));

    // Reverting the containment takes the witness and with it the non-emptiness away again
    solver.beginBacktrack(level + 1);
    solver.endBacktrack();
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(empty));
    EXPECT_FALSE(solver.assignedFalse(empty));
}

TEST(VerifyBackend, SetsEmptinessAndContainmentExclude) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    auto e = sets.newElement(solver);
    solver.propagate();

    // The emptiness forces the element out, which is the forall distribution
    solver.decideTrue(sets.isEmpty(solver, a));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(sets.assignedFalse(solver, e, Sets::in(a)));
    solver.backtrack(0);
    solver.propagate();

    // And the other way around the element forces the set to be non empty
    sets.decideTrue(solver, e, Sets::in(a));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(solver.assignedFalse(sets.isEmpty(solver, a)));
}

TEST(VerifyBackend, SetsEqualityPropagation1) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set c = solver.newAuxUninterpretedConstantSet();
    Set ab = sets.union_(solver, { a, b });
    Set ac = sets.union_(solver, { a, c });
    solver.addClause({ solver.equality(b, c) });
    Bool eq = solver.equality(ab, ac);
    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = sets.newElement(solver);
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(ab));
    solver.propagate();
    sets.decideTrue(solver, e, !Sets::in(ac));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.backtrack(0);
    solver.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    sets.decideTrue(solver, e, Sets::in(ac));
    solver.propagate();
    sets.decideTrue(solver, e, !Sets::in(ab));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.backtrack(0);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsEqualityPropagation2) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set c = solver.newAuxUninterpretedConstantSet();
    Set ab = sets.union_(solver, { a, b });
    Set ac = sets.union_(solver, { a, c });
    solver.addClause({ solver.equality(b, c) });
    Bool eq = solver.equality(ab, ac);
    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = sets.newElement(solver);
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { ab }, { ac })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    VERIFY(solver.currentDecisionLevel() == -1);
    solver.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { ac }, { ab })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    VERIFY(solver.currentDecisionLevel() == -1);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsUnionInterDistribution) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set c = solver.newAuxUninterpretedConstantSet();

    Set ibc = sets.intersection(solver, { b, c });
    Set lhs = sets.union_(solver, { a, ibc });
    Set uab = sets.union_(solver, { a, b });
    Set uac = sets.union_(solver, { a, c });
    Set rhs = sets.intersection(solver, { uab, uac });
    Bool eq = solver.equality(lhs, rhs);

    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = sets.newElement(solver);
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { rhs }, { lhs })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    VERIFY(solver.currentDecisionLevel() == -1);
    solver.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { lhs }, { rhs })));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    sets.decideTrue(solver, e, Sets::in(a));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    VERIFY(solver.currentDecisionLevel() == -1);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsProveDisequal) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set c = solver.newAuxUninterpretedConstantSet();

    solver.addClause({ sets.isEmpty(solver, sets.intersection(solver, { a, c })) });
    solver.addClause({ sets.isEmpty(solver, sets.intersection(solver, { b, c })) });
    solver.addClause({ !sets.isEmpty(solver, c) });
    Bool eq = solver.equality(sets.union_(solver, { a, c }), b);
    solver.propagate();

    EXPECT_FALSE(solver.assignedFalse(eq));
    solver.decideTrue(eq);
    EXPECT_FALSE(solver.hasConflicts());

    auto e = sets.newElement(solver);
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(c));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    EXPECT_EQ(solver.currentDecisionLevel(), -1);
    EXPECT_TRUE(solver.assignedFalse(eq));
}

TEST(VerifyBackend, SetsUnionOfEqualSets) {
    // Prove that union(A, B) = A if A = B
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set u = sets.union_(solver, { a, b });
    Bool eq = solver.equality(u, a);

    auto e = sets.newElement(solver);
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { a }, { u })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    solver.decideTrue(solver.equality(a, b));
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { u }, { a })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsSingletons) {
    // Prove that {a}u{b} = {a} iff a = b
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Value aValue = solver.newAuxUninterpretedConstant();
    Value bValue = solver.newAuxUninterpretedConstant();

    Set a = solver.uninterpConstantSingletons.singleton(solver, aValue);
    Set b = solver.uninterpConstantSingletons.singleton(solver, bValue);
    Set u = sets.union_(solver, { a, b });
    Bool valueEq = solver.equality(aValue, bValue);
    Bool eq = solver.equality(u, a);
    solver.propagate();

    auto e = sets.newElement(solver);
    solver.propagate();

    // 1. a = b implies {a}u{b} = {a}
    solver.decideTrue(valueEq);
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { a }, { u })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { u }, { a })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
    solver.backtrack(0);
    solver.propagate();

    // 2. {a}u{b} = {a} implies a = b
    solver.decideTrue(eq);
    solver.propagate();
    solver.decideTrue(!valueEq);
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(b));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(valueEq));
}

TEST(VerifyBackend, SetsExaustiveOnNewSet) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    auto e = sets.newElement(solver);
    solver.propagate();

    sets.decideTrue(solver, e, !Sets::in(a));
    solver.propagate();
    sets.decideTrue(solver, e, !Sets::in(b));
    solver.propagate();

    Set u = sets.union_(solver, { a, b });
    solver.propagate();
    EXPECT_TRUE(sets.assignedFalse(solver, e, Sets::in(u)));
}

TEST(VerifyBackend, SetsExaustiveOnNewSetNegativeOnForAllElement) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    solver.decideTrue(sets.isEmpty(solver, a));
    solver.propagate();
    solver.decideTrue(sets.isEmpty(solver, b));
    solver.propagate();

    Set u = sets.union_(solver, { a, b });
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(sets.isEmpty(solver, u)));
}

TEST(VerifyBackend, SetsExaustiveOnNewSetPositiveIgnoredOnForAllElement) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    solver.decideTrue(!sets.isEmpty(solver, a));
    solver.propagate();
    solver.decideTrue(!sets.isEmpty(solver, b));
    solver.propagate();

    Set i = sets.intersection(solver, { a, b });
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(!sets.isEmpty(solver, i)));
}

TEST(VerifyBackend, SetsExprToDefOnNewSet) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    auto e = sets.newElement(solver);
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(a));
    solver.propagate();

    Set u = sets.union_(solver, { a, b });
    solver.propagate();
    EXPECT_TRUE(sets.assignedTrue(solver, e, Sets::in(u)));
}

TEST(VerifyBackend, SetsExprToDefOnNewSetNegativeOnForAllElement) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    solver.decideTrue(sets.isEmpty(solver, a));
    solver.propagate();

    Set i = sets.intersection(solver, { a, b });
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(sets.isEmpty(solver, i)));
}

TEST(VerifyBackend, SetsExprToDefOnNewSetPositiveIgnoredOnForAllElement) {
    SolverImpl solver;
    auto& sets = solver.uninterpConstantSets;
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    solver.decideTrue(!sets.isEmpty(solver, a));
    solver.propagate();

    Set u = sets.union_(solver, { a, b });
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(!sets.isEmpty(solver, u)));
}

}