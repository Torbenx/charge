#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, InvariantSetsAreUniquePerLocation) {
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();
    Invariant i1(0);
    Invariant i2(1);

    InvariantSet whole = solver.inclusiveInvariantSet(d1, identity_member);
    InvariantSet below = solver.exclusiveInvariantSet(d1, identity_member);
    InvariantSet part = solver.inclusiveInvariantSet(d1, m);
    InvariantSet other = solver.inclusiveInvariantSet(d2, identity_member);
    InvariantSet above = solver.pathInvariantSet(d1, m);
    InvariantSet singleton = solver.invariantSingletonSet(d1, m, i1);
    InvariantSet otherSingleton = solver.invariantSingletonSet(d1, m, i2);

    EXPECT_TRUE(whole == solver.inclusiveInvariantSet(d1, identity_member));
    EXPECT_TRUE(below == solver.exclusiveInvariantSet(d1, identity_member));
    EXPECT_TRUE(above == solver.pathInvariantSet(d1, m));
    EXPECT_TRUE(singleton == solver.invariantSingletonSet(d1, m, i1));

    // The four kinds describe different sets, and so do the different locations and invariants
    EXPECT_FALSE(whole == below);
    EXPECT_FALSE(whole == part);
    EXPECT_FALSE(whole == other);
    EXPECT_FALSE(part == above);
    EXPECT_FALSE(singleton == otherSingleton);

    EXPECT_TRUE(solver.locationOf(part) == MemoryLocation(d1, m));
    EXPECT_TRUE(solver.locationOf(below) == MemoryLocation(d1, identity_member));
    EXPECT_TRUE(solver.locationOf(above) == MemoryLocation(d1, m));
    EXPECT_TRUE(solver.locationOf(singleton) == MemoryLocation(d1, m));
    EXPECT_TRUE(solver.invariantOf(singleton) == i1);
    EXPECT_TRUE(solver.invariantOf(otherSingleton) == i2);

    EXPECT_EQ(solver.valueCount(TheoryId::InclusiveLocationInvariantSets), 3);
    EXPECT_EQ(solver.valueCount(TheoryId::ExclusiveLocationInvariantSets), 1);
    EXPECT_EQ(solver.valueCount(TheoryId::PathInvariantSets), 1);
    EXPECT_EQ(solver.valueCount(TheoryId::InvariantSingletonSets), 2);
}

TEST(VerifyBackend, InvariantSetsLookup) {
    // Enough sets that the lookup tables have to rehash their entries
    auto [solver, _] = Solver::makeReference();
    std::vector<MemoryLocation> locations;
    std::vector<InvariantSet> inclusive;
    std::vector<InvariantSet> exclusive;
    std::vector<InvariantSet> paths;
    std::vector<InvariantSet> singletons;
    for (int_t i = 0; i < 8; i++) {
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        for (int_t j = 0; j < 32; j++) {
            locations.push_back({ declaration, solver.newAuxMemberVariable() });
            inclusive.push_back(solver.inclusiveInvariantSet(locations.back()));
            exclusive.push_back(solver.exclusiveInvariantSet(locations.back()));
            paths.push_back(solver.pathInvariantSet(locations.back()));
            singletons.push_back(solver.invariantSingletonSet(locations.back(), Invariant(j)));
        }
    }

    EXPECT_EQ(solver.valueCount(TheoryId::InclusiveLocationInvariantSets), (int_t)locations.size());
    EXPECT_EQ(solver.valueCount(TheoryId::ExclusiveLocationInvariantSets), (int_t)locations.size());
    EXPECT_EQ(solver.valueCount(TheoryId::PathInvariantSets), (int_t)locations.size());
    EXPECT_EQ(solver.valueCount(TheoryId::InvariantSingletonSets), (int_t)locations.size());
    for (int_t i = 0; i < (int_t)locations.size(); i++) {
        EXPECT_TRUE(solver.inclusiveInvariantSet(locations[i]) == inclusive[i]);
        EXPECT_TRUE(solver.exclusiveInvariantSet(locations[i]) == exclusive[i]);
        EXPECT_TRUE(solver.pathInvariantSet(locations[i]) == paths[i]);
        EXPECT_TRUE(solver.invariantSingletonSet(locations[i], Invariant(i % 32)) == singletons[i]);
        EXPECT_TRUE(solver.locationOf(inclusive[i]) == locations[i]);
        EXPECT_TRUE(solver.locationOf(exclusive[i]) == locations[i]);
        EXPECT_TRUE(solver.locationOf(paths[i]) == locations[i]);
        EXPECT_TRUE(solver.locationOf(singletons[i]) == locations[i]);
        EXPECT_TRUE(solver.invariantOf(singletons[i]) == Invariant(i % 32));
    }
}

TEST(VerifyBackend, InvariantSetsOfEqualLocations) {
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    InvariantSet a = solver.inclusiveInvariantSet(d1, m1);
    InvariantSet b = solver.inclusiveInvariantSet(d2, m2);
    Bool setEquality = solver.equality(a, b);

    auto e = solver.newSetElement(Sort::InvariantSet);
    solver.propagate();
    solver.decideTrue(e, Sets::in(a));
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
    EXPECT_TRUE(solver.assignedTrue(e, Sets::in(b)));
}

TEST(VerifyBackend, InvariantSetsOfEqualLocationsButDifferentKinds) {
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    // The kinds hold different invariants of the same location, so the same location says nothing here
    Bool setEquality = solver.equality(solver.inclusiveInvariantSet(d, m1),
        solver.exclusiveInvariantSet(d, m2));
    Bool pathEquality = solver.equality(solver.inclusiveInvariantSet(d, m1),
        solver.pathInvariantSet(d, m2));

    solver.decideTrue(solver.equality(m1, m2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_FALSE(solver.assignedTrue(setEquality));
    EXPECT_FALSE(solver.assignedTrue(pathEquality));
}

TEST(VerifyBackend, InvariantPathSetsOfEqualLocations) {
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    Bool setEquality = solver.equality(solver.pathInvariantSet(d1, m1),
        solver.pathInvariantSet(d2, m2));

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
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();
    Invariant i1(0);
    Invariant i2(1);

    Bool sameInvariant = solver.equality(solver.invariantSingletonSet(d, m1, i1),
        solver.invariantSingletonSet(d, m2, i1));
    Bool otherInvariant = solver.equality(solver.invariantSingletonSet(d, m1, i1),
        solver.invariantSingletonSet(d, m2, i2));

    solver.decideTrue(solver.equality(m1, m2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    // The location decides the singleton of one invariant, but the invariants of two of them are distinct
    EXPECT_TRUE(solver.assignedTrue(sameInvariant));
    EXPECT_FALSE(solver.assignedTrue(otherInvariant));
}

TEST(VerifyBackend, InvariantSingletonSetsHoldOneInvariant) {
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();
    Invariant i(0);

    InvariantSet a = solver.invariantSingletonSet(d, m1, i);
    InvariantSet b = solver.invariantSingletonSet(d, m2, i);

    auto e = solver.newSetElement(Sort::InvariantSet);
    solver.propagate();
    solver.decideTrue(e, Sets::in(a));
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(solver.equality(m1, m2)));

    // A singleton set holds nothing but the singleton of its invariant at its location, so a singleton of two of
    // them is the same singleton and the locations of the two are the same as well
    solver.decideTrue(e, Sets::in(b));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    EXPECT_TRUE(solver.assignedTrue(solver.equality(a, b)));
    EXPECT_TRUE(solver.assignedTrue(solver.equality(m1, m2)));
}

TEST(VerifyBackend, InvariantSingletonSetsOfDistinctInvariantsAreDisjoint) {
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    InvariantSet a = solver.invariantSingletonSet(d, m, Invariant(0));
    InvariantSet b = solver.invariantSingletonSet(d, m, Invariant(1));

    auto e = solver.newSetElement(Sort::InvariantSet);
    solver.propagate();
    solver.decideTrue(e, Sets::in(a));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    // The singletons of two invariants are distinct even at the same location, so no singleton is in both
    solver.decideTrue(e, Sets::in(b));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
}

TEST(VerifyBackend, InvariantSingletonSetsNeverEmpty) {
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    InvariantSet a = solver.invariantSingletonSet(d, m1, Invariant(0));
    InvariantSet b = solver.invariantSingletonSet(d, m2, Invariant(1));

    EXPECT_TRUE(solver.alwaysDisequal(a, b));
}

TEST(VerifyBackend, InvariantSingletonSetsAreBacktracked) {
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    InvariantSet a = solver.invariantSingletonSet(d, m, Invariant(0));
    InvariantSet b = solver.invariantSingletonSet(d, m, Invariant(1));

    auto e = solver.newSetElement(Sort::InvariantSet);
    solver.propagate();
    int_t levelBeforeContainment = solver.currentDecisionLevel();
    solver.decideTrue(e, Sets::in(a));
    solver.propagate();

    // Reverting the containment must forget the singleton set it was found in
    solver.beginBacktrack(levelBeforeContainment + 1);
    solver.endBacktrack();
    solver.checkInvariances();
    EXPECT_FALSE(solver.assignedTrue(e, Sets::in(a)));

    // So the singleton of another invariant is not compared against it anymore
    solver.decideTrue(e, Sets::in(b));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());
    solver.checkInvariances();
}

TEST(VerifyBackend, InvariantSetsInSetTheory) {
    // Invariant sets are ordinary sets, so the set theory reasoning applies to them
    auto [solver, _] = Solver::makeReference();
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();
    Invariant i(0);

    InvariantSet whole = solver.inclusiveInvariantSet(d, identity_member);
    InvariantSet singleton = solver.invariantSingletonSet(d, m, i);
    Set u = solver.union_({ whole, singleton });

    Bool eq = solver.equality(u, whole);
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(eq));

    // There are no rules relating a location to the invariants below it yet, so the subset relation must
    // be asserted explicitly.
    auto e = solver.newSetElement(Sort::InvariantSet);
    solver.propagate();
    solver.addClause({ solver.equality(solver.subset({ singleton }, { whole }), solver.emptySet(Sort::InvariantSet)) });
    solver.propagate();

    solver.decideTrue(e, Sets::in(solver.subset({ u }, { whole })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    solver.decideTrue(e, Sets::in(solver.subset({ whole }, { u })));
    solver.propagate();
    EXPECT_TRUE(solver.hasConflicts());
    solver.analyzeConflicts();
    solver.propagate();

    EXPECT_TRUE(solver.assignedTrue(eq));
}

}
