#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, MemoryLocationSetsAreUniquePerLocation) {
    SolverImpl solver;
    auto& memorySets = solver.memorySets;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    Value whole = memorySets.set(solver, d1, identity_member);
    Value part = memorySets.set(solver, d1, m);
    Value other = memorySets.set(solver, d2, identity_member);

    EXPECT_TRUE(whole == memorySets.set(solver, d1, identity_member));
    EXPECT_TRUE(part == memorySets.set(solver, d1, m));
    EXPECT_FALSE(whole == part);
    EXPECT_FALSE(whole == other);

    EXPECT_TRUE(memorySets.locationOf(part) == MemoryLocation(d1, m));
    EXPECT_TRUE(memorySets.locationOf(other) == MemoryLocation(d2, identity_member));

    EXPECT_EQ(solver.valueCount(TheoryId::MemoryLocationSets), 3);
}

TEST(VerifyBackend, MemoryLocationSetsLookup) {
    // Enough locations that the lookup table has to rehash its entries
    SolverImpl solver;
    auto& memorySets = solver.memorySets;
    std::vector<MemoryLocation> locations;
    std::vector<Value> sets;
    for (int_t i = 0; i < 8; i++) {
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        for (int_t j = 0; j < 32; j++) {
            locations.push_back({ declaration, solver.newAuxMemberVariable() });
            sets.push_back(memorySets.set(solver, locations.back()));
        }
    }

    EXPECT_EQ(solver.valueCount(TheoryId::MemoryLocationSets), (int_t)locations.size());
    for (int_t i = 0; i < (int_t)locations.size(); i++) {
        EXPECT_TRUE(memorySets.set(solver, locations[i]) == sets[i]);
        EXPECT_TRUE(memorySets.locationOf(sets[i]) == locations[i]);
    }
}

TEST(VerifyBackend, MemoryLocationSetsOfEqualDeclarations) {
    SolverImpl solver;
    auto& sets = solver.memorySetsBaseTheory;
    auto& memorySets = solver.memorySets;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    Value a = memorySets.set(solver, d1, m);
    Value b = memorySets.set(solver, d2, m);
    Bool setEquality = solver.equality(a, b);

    auto e = sets.newElement(solver);
    solver.sat.propagate();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.sat.propagate();

    solver.decideTrue(solver.equality(d1, d2));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    // The two pairs describe the same location, so the sets of that location are the same and so is
    // everything that is known about them
    EXPECT_TRUE(solver.assignedTrue(setEquality));
    EXPECT_TRUE(sets.assignedTrue(solver, e, Sets::in(b)));
}

TEST(VerifyBackend, MemoryLocationSetsOfEqualMembers) {
    SolverImpl solver;
    auto& sets = solver.memorySetsBaseTheory;
    auto& memorySets = solver.memorySets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    Value a = memorySets.set(solver, d, m1);
    Value b = memorySets.set(solver, d, m2);
    Bool setEquality = solver.equality(a, b);

    auto e = sets.newElement(solver);
    solver.sat.propagate();
    sets.decideTrue(solver, e, Sets::in(a));
    solver.sat.propagate();

    // The member of a location decides it just as much as its declaration does
    solver.decideTrue(solver.equality(m1, m2));
    solver.sat.propagate();
    EXPECT_FALSE(solver.sat.hasConflicts());

    EXPECT_TRUE(solver.assignedTrue(setEquality));
    EXPECT_TRUE(sets.assignedTrue(solver, e, Sets::in(b)));
}

TEST(VerifyBackend, MemoryLocationSetsNeedBothPartsOfTheLocation) {
    SolverImpl solver;
    auto& memorySets = solver.memorySets;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    Value a = memorySets.set(solver, d1, m1);
    Value b = memorySets.set(solver, d2, m2);
    Bool setEquality = solver.equality(a, b);
    solver.sat.propagate();

    // One half of the location being the same says nothing about the sets
    solver.decideTrue(solver.equality(m1, m2));
    solver.sat.propagate();
    EXPECT_FALSE(solver.assignedTrue(setEquality));
    EXPECT_FALSE(solver.assignedFalse(setEquality));

    solver.decideTrue(solver.equality(d1, d2));
    solver.sat.propagate();
    EXPECT_TRUE(solver.assignedTrue(setEquality));
}

TEST(VerifyBackend, MemoryLocationSetsInSetTheory) {
    // Location sets are ordinary sets, so the set theory reasoning applies to them
    SolverImpl solver;
    auto& sets = solver.memorySetsBaseTheory;
    auto& memorySets = solver.memorySets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    Value whole = memorySets.set(solver, d, identity_member);
    Value part = memorySets.set(solver, d, m);
    Value u = sets.union_(solver, { whole, part });

    Bool eq = solver.equality(u, whole);
    solver.sat.propagate();
    EXPECT_FALSE(solver.assignedTrue(eq));

    // There are no rules relating a location to its members yet, so the subset relation must be
    // asserted explicitly.
    auto e = sets.newElement(solver);
    solver.sat.propagate();
    solver.addClause({ solver.equality(sets.subset(solver, { part }, { whole }), sets.emptySet()) });
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
