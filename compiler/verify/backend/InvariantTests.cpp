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

    Value whole = invariantSets.inclusiveSet(solver, d1, identity_member);
    Value below = invariantSets.exclusiveSet(solver, d1, identity_member);
    Value part = invariantSets.inclusiveSet(solver, d1, m);
    Value other = invariantSets.inclusiveSet(solver, d2, identity_member);
    Value leaf = invariantSets.leafSet(solver, d1, m, i1);
    Value otherLeaf = invariantSets.leafSet(solver, d1, m, i2);

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
    std::vector<Value> inclusive;
    std::vector<Value> exclusive;
    std::vector<Value> leafs;
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

    Value a = invariantSets.inclusiveSet(solver, d1, m1);
    Value b = invariantSets.inclusiveSet(solver, d2, m2);
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

TEST(VerifyBackend, InvariantSetsInSetTheory) {
    // Invariant sets are ordinary sets, so the set theory reasoning applies to them
    SolverImpl solver;
    auto& sets = solver.invariantSetsBaseTheory;
    auto& invariantSets = solver.invariantSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();
    Invariant i(0);

    Value whole = invariantSets.inclusiveSet(solver, d, identity_member);
    Value leaf = invariantSets.leafSet(solver, d, m, i);
    Value u = sets.union_(solver, { whole, leaf });

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
