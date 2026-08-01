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
    InvariantSet leaf = invariantSets.leafSet(solver, d1, m, i1);
    InvariantSet otherLeaf = invariantSets.leafSet(solver, d1, m, i2);

    EXPECT_TRUE(whole == invariantSets.inclusiveSet(solver, d1, identity_member));
    EXPECT_TRUE(below == invariantSets.exclusiveSet(solver, d1, identity_member));
    EXPECT_TRUE(leaf == invariantSets.leafSet(solver, d1, m, i1));

    // The three kinds describe different sets, and so do the different locations and invariants
    EXPECT_FALSE(whole == below);
    EXPECT_FALSE(whole == part);
    EXPECT_FALSE(whole == other);
    EXPECT_FALSE(leaf == otherLeaf);

    EXPECT_TRUE(invariantSets.locationOf(part) == MemoryLocation(d1, m));
    EXPECT_TRUE(invariantSets.locationOf(below) == MemoryLocation(d1, identity_member));
    EXPECT_TRUE(invariantSets.locationOf(leaf) == MemoryLocation(d1, m));
    EXPECT_TRUE(invariantSets.invariantOf(leaf) == i1);
    EXPECT_TRUE(invariantSets.invariantOf(otherLeaf) == i2);

    EXPECT_EQ(solver.valueCount(TheoryId::InclusiveLocationInvariantSets), 3);
    EXPECT_EQ(solver.valueCount(TheoryId::ExclusiveLocationInvariantSets), 1);
    EXPECT_EQ(solver.valueCount(TheoryId::LeafInvariantSets), 2);
}

TEST(VerifyBackend, InvariantSetsLookup) {
    // Enough sets that the lookup tables have to rehash their entries
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    std::vector<MemoryLocation> locations;
    std::vector<InvariantSet> inclusive;
    std::vector<InvariantSet> exclusive;
    std::vector<InvariantSet> leafs;
    for (int_t i = 0; i < 8; i++) {
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        for (int_t j = 0; j < 32; j++) {
            locations.push_back({ declaration, solver.newAuxMemberVariable() });
            inclusive.push_back(invariantSets.inclusiveSet(solver, locations.back()));
            exclusive.push_back(invariantSets.exclusiveSet(solver, locations.back()));
            leafs.push_back(invariantSets.leafSet(solver, locations.back(), Invariant(j)));
        }
    }

    EXPECT_EQ(solver.valueCount(TheoryId::InclusiveLocationInvariantSets), (int_t)locations.size());
    EXPECT_EQ(solver.valueCount(TheoryId::ExclusiveLocationInvariantSets), (int_t)locations.size());
    EXPECT_EQ(solver.valueCount(TheoryId::LeafInvariantSets), (int_t)locations.size());
    for (int_t i = 0; i < (int_t)locations.size(); i++) {
        EXPECT_TRUE(invariantSets.inclusiveSet(solver, locations[i]) == inclusive[i]);
        EXPECT_TRUE(invariantSets.exclusiveSet(solver, locations[i]) == exclusive[i]);
        EXPECT_TRUE(invariantSets.leafSet(solver, locations[i], Invariant(i % 32)) == leafs[i]);
        EXPECT_TRUE(invariantSets.locationOf(inclusive[i]) == locations[i]);
        EXPECT_TRUE(invariantSets.locationOf(exclusive[i]) == locations[i]);
        EXPECT_TRUE(invariantSets.locationOf(leafs[i]) == locations[i]);
        EXPECT_TRUE(invariantSets.invariantOf(leafs[i]) == Invariant(i % 32));
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
    solver.sat.propagate();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.sat.propagate();

    // One half of the location being the same says nothing about the sets
    solver.decideTrue(solver.equality(m1, m2));
    solver.sat.propagate();
    EXPECT_FALSE(solver.assignedTrue(setEquality));
    EXPECT_FALSE(solver.assignedFalse(setEquality));

    // The two pairs describe the same location, so the sets of that location are the same and so is
    // everything that is known about them
    solver.decideTrue(solver.equality(d1, d2));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());
    EXPECT_TRUE(solver.assignedTrue(setEquality));
    EXPECT_TRUE(sets.assignedTrue(solver, e, Sets::in(b)));
}

TEST(VerifyBackend, InvariantSetsOfEqualLocationsButDifferentKinds) {
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    // The kinds hold different leafs of the same location, so the same location says nothing here
    Bool setEquality = solver.equality(invariantSets.inclusiveSet(solver, d, m1),
        invariantSets.exclusiveSet(solver, d, m2));

    solver.decideTrue(solver.equality(m1, m2));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());
    EXPECT_FALSE(solver.assignedTrue(setEquality));
}

TEST(VerifyBackend, InvariantLeafSetsNeedTheSameInvariant) {
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();
    Invariant i1(0);
    Invariant i2(1);

    Bool sameInvariant = solver.equality(invariantSets.leafSet(solver, d, m1, i1),
        invariantSets.leafSet(solver, d, m2, i1));
    Bool otherInvariant = solver.equality(invariantSets.leafSet(solver, d, m1, i1),
        invariantSets.leafSet(solver, d, m2, i2));

    solver.decideTrue(solver.equality(m1, m2));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    // The location decides the leaf of one invariant, but the leafs of two of them are distinct
    EXPECT_TRUE(solver.assignedTrue(sameInvariant));
    EXPECT_FALSE(solver.assignedTrue(otherInvariant));
}

TEST(VerifyBackend, InvariantLeafSetsHoldOneLeaf) {
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();
    Invariant i(0);

    InvariantSet a = invariantSets.leafSet(solver, d, m1, i);
    InvariantSet b = invariantSets.leafSet(solver, d, m2, i);

    auto e = sets.newElement(solver);
    solver.sat.propagate();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.sat.propagate();
    EXPECT_FALSE(solver.assignedTrue(solver.equality(m1, m2)));

    // A leaf set holds nothing but the leaf of its invariant at its location, so a leaf of two of
    // them is the same leaf and the locations of the two are the same as well
    sets.decideTrue(solver, e, Sets::in(b));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());
    EXPECT_TRUE(solver.assignedTrue(solver.equality(a, b)));
    EXPECT_TRUE(solver.assignedTrue(solver.equality(m1, m2)));
}

TEST(VerifyBackend, InvariantLeafSetsOfDistinctInvariantsAreDisjoint) {
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    InvariantSet a = invariantSets.leafSet(solver, d, m, Invariant(0));
    InvariantSet b = invariantSets.leafSet(solver, d, m, Invariant(1));

    auto e = sets.newElement(solver);
    solver.sat.propagate();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    // The leafs of two invariants are distinct even at the same location, so no leaf is in both
    sets.decideTrue(solver, e, Sets::in(b));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
}

TEST(VerifyBackend, InvariantLeafSetsNeverEmpty) {
    SolverImpl solver;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    InvariantSet a = invariantSets.leafSet(solver, d, m1, Invariant(0));
    InvariantSet b = invariantSets.leafSet(solver, d, m2, Invariant(1));

    EXPECT_TRUE(solver.alwaysDisequal(a, b));
}

TEST(VerifyBackend, InvariantLeafSetsAreBacktracked) {
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    InvariantSet a = invariantSets.leafSet(solver, d, m, Invariant(0));
    InvariantSet b = invariantSets.leafSet(solver, d, m, Invariant(1));

    auto e = sets.newElement(solver);
    solver.sat.propagate();
    int_t levelBeforeContainment = solver.currentDecisionLevel();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.sat.propagate();

    // Reverting the containment must forget the leaf set it was found in
    solver.sat.beginBacktrack(levelBeforeContainment + 1);
    solver.sat.endBacktrack();
    invariantSets.checkInvariances(solver);
    EXPECT_FALSE(sets.assignedTrue(solver, e, Sets::in(a)));

    // So the leaf of another invariant is not compared against it anymore
    sets.decideTrue(solver, e, Sets::in(b));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());
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
    InvariantSet leaf = invariantSets.leafSet(solver, d, m, i);
    Set u = sets.union_(solver, { whole, leaf });

    Bool eq = solver.equality(u, whole);
    solver.sat.propagate();
    EXPECT_FALSE(solver.assignedTrue(eq));

    // There are no rules relating a location to the leafs below it yet, so the subset relation must
    // be asserted explicitly.
    auto e = sets.newElement(solver);
    solver.sat.propagate();
    solver.addClause({ solver.equality(sets.subset(solver, { leaf }, { whole }), sets.emptySet()) });
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { u }, { whole })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();

    sets.decideTrue(solver, e, Sets::in(sets.subset(solver, { whole }, { u })));
    solver.sat.propagate();
    EXPECT_TRUE(solver.sat.hasConflicts());
    solver.sat.analyzeConflicts();
    solver.sat.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

}
