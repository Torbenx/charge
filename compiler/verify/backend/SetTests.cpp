#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, SetsEmptySetBasics) {
    auto [solver, _] = Solver::makeReference();
    Set emptySet = solver.emptySet(Sort::UninterpretedConstantSet);

    Bool emptySetIsEmpty = solver.isEmpty(emptySet);
    EXPECT_TRUE(solver.alwaysTrue(emptySetIsEmpty));

    Bool emptySetEqEmptySet = solver.equality(emptySet, emptySet);
    EXPECT_TRUE(solver.alwaysTrue(emptySetEqEmptySet));

    auto element = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(element, !Sets::in(emptySet)));
}

TEST(VerifyBackend, SetsContainedElementWitnessesNonEmpty) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    Bool empty = solver.isEmpty(a);
    EXPECT_FALSE(solver.assignedTrue(empty));
    EXPECT_FALSE(solver.assignedFalse(empty));

    int_t level = solver.currentDecisionLevel();
    solver.decideTrue(e, Sets::in(a));
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
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    // The emptiness forces the element out, which is the forall distribution
    solver.decideTrue(solver.isEmpty(a));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(solver.assignedFalse(e, Sets::in(a)));
    solver.backtrack(0);
    solver.propagate();

    // And the other way around the element forces the set to be non empty
    solver.decideTrue(e, Sets::in(a));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(solver.assignedFalse(solver.isEmpty(a)));
}

TEST(VerifyBackend, SetsEqualityPropagation1) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set c = solver.newAuxUninterpretedConstantSet();
    Set ab = solver.union_({ a, b });
    Set ac = solver.union_({ a, c });
    solver.addClause({ solver.equality(b, c) });
    Bool eq = solver.equality(ab, ac);
    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    solver.decideTrue(e, Sets::in(ab));
    solver.propagate();
    solver.decideTrue(e, !Sets::in(ac));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.backtrack(0);
    solver.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    solver.decideTrue(e, Sets::in(ac));
    solver.propagate();
    solver.decideTrue(e, !Sets::in(ab));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.backtrack(0);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsEqualityPropagation2) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set c = solver.newAuxUninterpretedConstantSet();
    Set ab = solver.union_({ a, b });
    Set ac = solver.union_({ a, c });
    solver.addClause({ solver.equality(b, c) });
    Bool eq = solver.equality(ab, ac);
    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    solver.decideTrue(e, Sets::in(solver.subset({ ab }, { ac })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    VERIFY(solver.currentDecisionLevel() == -1);
    solver.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    solver.decideTrue(e, Sets::in(solver.subset({ ac }, { ab })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    VERIFY(solver.currentDecisionLevel() == -1);
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsUnionInterDistribution) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set c = solver.newAuxUninterpretedConstantSet();

    Set ibc = solver.intersection({ b, c });
    Set lhs = solver.union_({ a, ibc });
    Set uab = solver.union_({ a, b });
    Set uac = solver.union_({ a, c });
    Set rhs = solver.intersection({ uab, uac });
    Bool eq = solver.equality(lhs, rhs);

    EXPECT_FALSE(solver.assignedTrue(eq));

    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    solver.decideTrue(e, Sets::in(solver.subset({ rhs }, { lhs })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    VERIFY(solver.currentDecisionLevel() == -1);
    solver.propagate();

    EXPECT_FALSE(solver.assignedTrue(eq));

    solver.decideTrue(e, Sets::in(solver.subset({ lhs }, { rhs })));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    solver.decideTrue(e, Sets::in(a));
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
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set c = solver.newAuxUninterpretedConstantSet();

    solver.addClause({ solver.isEmpty(solver.intersection({ a, c })) });
    solver.addClause({ solver.isEmpty(solver.intersection({ b, c })) });
    solver.addClause({ !solver.isEmpty(c) });
    Bool eq = solver.equality(solver.union_({ a, c }), b);
    solver.propagate();

    EXPECT_FALSE(solver.assignedFalse(eq));
    solver.decideTrue(eq);
    EXPECT_FALSE(solver.hasConflicts());

    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    solver.decideTrue(e, Sets::in(c));
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
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();
    Set u = solver.union_({ a, b });
    Bool eq = solver.equality(u, a);

    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    solver.decideTrue(e, Sets::in(solver.subset({ a }, { u })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    solver.decideTrue(solver.equality(a, b));
    solver.propagate();

    solver.decideTrue(e, Sets::in(solver.subset({ u }, { a })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

TEST(VerifyBackend, SetsSingletons) {
    // Prove that {a}u{b} = {a} iff a = b
    auto [solver, _] = Solver::makeReference();
    Value aValue = solver.newAuxUninterpretedConstant();
    Value bValue = solver.newAuxUninterpretedConstant();

    Set a = solver.singleton(aValue);
    Set b = solver.singleton(bValue);
    Set u = solver.union_({ a, b });
    Bool valueEq = solver.equality(aValue, bValue);
    Bool eq = solver.equality(u, a);
    solver.propagate();

    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    // 1. a = b implies {a}u{b} = {a}
    solver.decideTrue(valueEq);
    solver.propagate();

    solver.decideTrue(e, Sets::in(solver.subset({ a }, { u })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    solver.decideTrue(e, Sets::in(solver.subset({ u }, { a })));
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

    solver.decideTrue(e, Sets::in(b));
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
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    solver.decideTrue(e, !Sets::in(a));
    solver.propagate();
    solver.decideTrue(e, !Sets::in(b));
    solver.propagate();

    Set u = solver.union_({ a, b });
    solver.propagate();
    EXPECT_TRUE(solver.assignedFalse(e, Sets::in(u)));
}

TEST(VerifyBackend, SetsExaustiveOnNewSetNegativeOnForAllElement) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    solver.decideTrue(solver.isEmpty(a));
    solver.propagate();
    solver.decideTrue(solver.isEmpty(b));
    solver.propagate();

    Set u = solver.union_({ a, b });
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(solver.isEmpty(u)));
}

TEST(VerifyBackend, SetsExaustiveOnNewSetPositiveIgnoredOnForAllElement) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    solver.decideTrue(!solver.isEmpty(a));
    solver.propagate();
    solver.decideTrue(!solver.isEmpty(b));
    solver.propagate();

    Set i = solver.intersection({ a, b });
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(!solver.isEmpty(i)));
}

TEST(VerifyBackend, SetsExprToDefOnNewSet) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    auto e = solver.newSetElement(Sort::UninterpretedConstantSet);
    solver.propagate();

    solver.decideTrue(e, Sets::in(a));
    solver.propagate();

    Set u = solver.union_({ a, b });
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(e, Sets::in(u)));
}

TEST(VerifyBackend, SetsExprToDefOnNewSetNegativeOnForAllElement) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    solver.decideTrue(solver.isEmpty(a));
    solver.propagate();

    Set i = solver.intersection({ a, b });
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(solver.isEmpty(i)));
}

TEST(VerifyBackend, SetsExprToDefOnNewSetPositiveIgnoredOnForAllElement) {
    auto [solver, _] = Solver::makeReference();
    Set a = solver.newAuxUninterpretedConstantSet();
    Set b = solver.newAuxUninterpretedConstantSet();

    solver.decideTrue(!solver.isEmpty(a));
    solver.propagate();

    Set u = solver.union_({ a, b });
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(!solver.isEmpty(u)));
}

}