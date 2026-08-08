#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, InvariantSetsAreUniquePerLocation) {
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();
    Invariant i1(0);
    Invariant i2(1);

    InvariantSet whole = invariantSets.inclusiveSet(solver, d1, identity_member);
    InvariantSet below = invariantSets.exclusiveSet(solver, d1, identity_member);
    InvariantSet part = invariantSets.inclusiveSet(solver, d1, m);
    InvariantSet other = invariantSets.inclusiveSet(solver, d2, identity_member);
    InvariantSet above = invariantSets.pathSet(solver, d1, m);
    InvariantSet singleton = invariantSets.singletonSet(solver, d1, m, i1);
    InvariantSet otherSingleton = invariantSets.singletonSet(solver, d1, m, i2);

    EXPECT_TRUE(whole == invariantSets.inclusiveSet(solver, d1, identity_member));
    EXPECT_TRUE(below == invariantSets.exclusiveSet(solver, d1, identity_member));
    EXPECT_TRUE(above == invariantSets.pathSet(solver, d1, m));
    EXPECT_TRUE(singleton == invariantSets.singletonSet(solver, d1, m, i1));

    // The four kinds describe different sets, and so do the different locations and invariants
    EXPECT_FALSE(whole == below);
    EXPECT_FALSE(whole == part);
    EXPECT_FALSE(whole == other);
    EXPECT_FALSE(part == above);
    EXPECT_FALSE(singleton == otherSingleton);

    EXPECT_TRUE(invariantSets.locationOf(part) == MemoryLocation(d1, m));
    EXPECT_TRUE(invariantSets.locationOf(below) == MemoryLocation(d1, identity_member));
    EXPECT_TRUE(invariantSets.locationOf(above) == MemoryLocation(d1, m));
    EXPECT_TRUE(invariantSets.locationOf(singleton) == MemoryLocation(d1, m));
    EXPECT_TRUE(invariantSets.invariantOf(singleton) == i1);
    EXPECT_TRUE(invariantSets.invariantOf(otherSingleton) == i2);

    EXPECT_EQ(solver.valueCount(TheoryId::InclusiveLocationInvariantSets), 3);
    EXPECT_EQ(solver.valueCount(TheoryId::ExclusiveLocationInvariantSets), 1);
    EXPECT_EQ(solver.valueCount(TheoryId::PathInvariantSets), 1);
    EXPECT_EQ(solver.valueCount(TheoryId::InvariantSingletonSets), 2);
}

TEST(VerifyBackend, InvariantSetsLookup) {
    // Enough sets that the lookup tables have to rehash their entries
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    std::vector<MemoryLocation> locations;
    std::vector<InvariantSet> inclusive;
    std::vector<InvariantSet> exclusive;
    std::vector<InvariantSet> paths;
    std::vector<InvariantSet> singletons;
    for (int_t i = 0; i < 8; i++) {
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        for (int_t j = 0; j < 32; j++) {
            locations.push_back({ declaration, solver.newAuxMemberVariable() });
            inclusive.push_back(invariantSets.inclusiveSet(solver, locations.back()));
            exclusive.push_back(invariantSets.exclusiveSet(solver, locations.back()));
            paths.push_back(invariantSets.pathSet(solver, locations.back()));
            singletons.push_back(invariantSets.singletonSet(solver, locations.back(), Invariant(j)));
        }
    }

    EXPECT_EQ(solver.valueCount(TheoryId::InclusiveLocationInvariantSets), (int_t)locations.size());
    EXPECT_EQ(solver.valueCount(TheoryId::ExclusiveLocationInvariantSets), (int_t)locations.size());
    EXPECT_EQ(solver.valueCount(TheoryId::PathInvariantSets), (int_t)locations.size());
    EXPECT_EQ(solver.valueCount(TheoryId::InvariantSingletonSets), (int_t)locations.size());
    for (int_t i = 0; i < (int_t)locations.size(); i++) {
        EXPECT_TRUE(invariantSets.inclusiveSet(solver, locations[i]) == inclusive[i]);
        EXPECT_TRUE(invariantSets.exclusiveSet(solver, locations[i]) == exclusive[i]);
        EXPECT_TRUE(invariantSets.pathSet(solver, locations[i]) == paths[i]);
        EXPECT_TRUE(invariantSets.singletonSet(solver, locations[i], Invariant(i % 32)) == singletons[i]);
        EXPECT_TRUE(invariantSets.locationOf(inclusive[i]) == locations[i]);
        EXPECT_TRUE(invariantSets.locationOf(exclusive[i]) == locations[i]);
        EXPECT_TRUE(invariantSets.locationOf(paths[i]) == locations[i]);
        EXPECT_TRUE(invariantSets.locationOf(singletons[i]) == locations[i]);
        EXPECT_TRUE(invariantSets.invariantOf(singletons[i]) == Invariant(i % 32));
    }
}

TEST(VerifyBackend, InvariantSetsOfEqualLocations) {
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    InvariantSet a = invariantSets.inclusiveSet(solver, d1, m1);
    InvariantSet b = invariantSets.inclusiveSet(solver, d2, m2);
    Bool setEquality = solver.equality(a, b);

    auto e = sets.newElement(solver);
    solver.propagate();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.propagate();

    // One half of the location being the same says nothing about the sets
    solver.decideTrue(solver.equality(m1, m2));
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(setEquality));
    EXPECT_FALSE(solver.assignedFalse(setEquality));

    // The two pairs describe the same location, so the sets of that location are the same and so is
    // everything that is known about them
    solver.decideTrue(solver.equality(d1, d2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(solver.assignedTrue(setEquality));
    EXPECT_TRUE(sets.assignedTrue(solver, e, Sets::in(b)));
}

TEST(VerifyBackend, InvariantSetsOfEqualLocationsButDifferentKinds) {
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    // The kinds hold different invariants of the same location, so the same location says nothing here
    Bool setEquality = solver.equality(invariantSets.inclusiveSet(solver, d, m1),
        invariantSets.exclusiveSet(solver, d, m2));
    Bool pathEquality = solver.equality(invariantSets.inclusiveSet(solver, d, m1),
        invariantSets.pathSet(solver, d, m2));

    solver.decideTrue(solver.equality(m1, m2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_FALSE(solver.assignedTrue(setEquality));
    EXPECT_FALSE(solver.assignedTrue(pathEquality));
}

TEST(VerifyBackend, InvariantPathSetsOfEqualLocations) {
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    Bool setEquality = solver.equality(invariantSets.pathSet(solver, d1, m1),
        invariantSets.pathSet(solver, d2, m2));

    // One half of the location being the same says nothing about the sets
    solver.decideTrue(solver.equality(m1, m2));
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(setEquality));

    // Two locations that are the same have the same locations above them, so the paths are the same
    solver.decideTrue(solver.equality(d1, d2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(solver.assignedTrue(setEquality));
}

TEST(VerifyBackend, InvariantSingletonSetsNeedTheSameInvariant) {
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();
    Invariant i1(0);
    Invariant i2(1);

    Bool sameInvariant = solver.equality(invariantSets.singletonSet(solver, d, m1, i1),
        invariantSets.singletonSet(solver, d, m2, i1));
    Bool otherInvariant = solver.equality(invariantSets.singletonSet(solver, d, m1, i1),
        invariantSets.singletonSet(solver, d, m2, i2));

    solver.decideTrue(solver.equality(m1, m2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    // The location decides the singleton of one invariant, but the invariants of two of them are distinct
    EXPECT_TRUE(solver.assignedTrue(sameInvariant));
    EXPECT_FALSE(solver.assignedTrue(otherInvariant));
}

TEST(VerifyBackend, InvariantSingletonSetsHoldOneInvariant) {
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();
    Invariant i(0);

    InvariantSet a = invariantSets.singletonSet(solver, d, m1, i);
    InvariantSet b = invariantSets.singletonSet(solver, d, m2, i);

    auto e = sets.newElement(solver);
    solver.propagate();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(solver.equality(m1, m2)));

    // A singleton set holds nothing but the singleton of its invariant at its location, so a singleton of two of
    // them is the same singleton and the locations of the two are the same as well
    sets.decideTrue(solver, e, Sets::in(b));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(solver.assignedTrue(solver.equality(a, b)));
    EXPECT_TRUE(solver.assignedTrue(solver.equality(m1, m2)));
}

TEST(VerifyBackend, InvariantSingletonSetsOfDistinctInvariantsAreDisjoint) {
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    InvariantSet a = invariantSets.singletonSet(solver, d, m, Invariant(0));
    InvariantSet b = invariantSets.singletonSet(solver, d, m, Invariant(1));

    auto e = sets.newElement(solver);
    solver.propagate();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    // The singletons of two invariants are distinct even at the same location, so no singleton is in both
    sets.decideTrue(solver, e, Sets::in(b));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
}

TEST(VerifyBackend, InvariantSingletonSetsNeverEmpty) {
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    InvariantSet a = invariantSets.singletonSet(solver, d, m1, Invariant(0));
    InvariantSet b = invariantSets.singletonSet(solver, d, m2, Invariant(1));

    EXPECT_TRUE(solver.alwaysDisequal(a, b));
}

TEST(VerifyBackend, InvariantSingletonSetsAreBacktracked) {
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    InvariantSet a = invariantSets.singletonSet(solver, d, m, Invariant(0));
    InvariantSet b = invariantSets.singletonSet(solver, d, m, Invariant(1));

    auto e = sets.newElement(solver);
    solver.propagate();
    int_t levelBeforeContainment = solver.currentDecisionLevel();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.propagate();

    // Reverting the containment must forget the singleton set it was found in
    solver.beginBacktrack(levelBeforeContainment + 1);
    solver.endBacktrack();
    invariantSets.checkInvariances(solver);
    EXPECT_FALSE(sets.assignedTrue(solver, e, Sets::in(a)));

    // So the singleton of another invariant is not compared against it anymore
    sets.decideTrue(solver, e, Sets::in(b));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    invariantSets.checkInvariances(solver);
}

TEST(VerifyBackend, InvariantSetsInSetTheory) {
    // Invariant sets are ordinary sets, so the set theory reasoning applies to them
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();
    Invariant i(0);

    InvariantSet whole = invariantSets.inclusiveSet(solver, d, identity_member);
    InvariantSet singleton = invariantSets.singletonSet(solver, d, m, i);
    Set u = sets.union_(solver, { whole, singleton });

    Bool eq = solver.equality(u, whole);
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(eq));

    // There are no rules relating a location to the invariants below it yet, so the subset relation must
    // be asserted explicitly.
    auto e = sets.newElement(solver);
    solver.propagate();
    solver.addClause({ solver.equality(sets.subset(solver, { singleton }, { whole }), sets.emptySet()) });
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { u }, { whole })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { whole }, { u })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

}
