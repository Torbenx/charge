#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, MemoryLocationSetsAreUniquePerLocation) {
    SolverImpl solver;
    auto& locationSets = solver.memoryLocationSets;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    Value whole = locationSets.set(solver, d1, identity_member);
    Value part = locationSets.set(solver, d1, m);
    Value other = locationSets.set(solver, d2, identity_member);

    EXPECT_TRUE(whole == locationSets.set(solver, d1, identity_member));
    EXPECT_TRUE(part == locationSets.set(solver, d1, m));
    EXPECT_FALSE(whole == part);
    EXPECT_FALSE(whole == other);

    EXPECT_TRUE(locationSets.locationOf(part) == MemoryLocation(d1, m));
    EXPECT_TRUE(locationSets.locationOf(other) == MemoryLocation(d2, identity_member));

    EXPECT_EQ(solver.valueCount(TheoryId::MemoryLocationSets), 3);
}

TEST(VerifyBackend, MemoryLocationSetsLookup) {
    // Enough locations that the lookup table has to rehash its entries
    SolverImpl solver;
    auto& locationSets = solver.memoryLocationSets;
    std::vector<MemoryLocation> locations;
    std::vector<Value> sets;
    for (int_t i = 0; i < 8; i++) {
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        for (int_t j = 0; j < 32; j++) {
            locations.push_back({ declaration, solver.newAuxMemberVariable() });
            sets.push_back(locationSets.set(solver, locations.back()));
        }
    }

    EXPECT_EQ(solver.valueCount(TheoryId::MemoryLocationSets), (int_t)locations.size());
    for (int_t i = 0; i < (int_t)locations.size(); i++) {
        EXPECT_TRUE(locationSets.set(solver, locations[i]) == sets[i]);
        EXPECT_TRUE(locationSets.locationOf(sets[i]) == locations[i]);
    }
}

TEST(VerifyBackend, MemoryLocationSetsInSetTheory) {
    // Location sets are ordinary sets, so the set theory reasoning applies to them
    SolverImpl solver;
    auto& sets = solver.memorySets;
    auto& locationSets = solver.memoryLocationSets;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    Value whole = locationSets.set(solver, d, identity_member);
    Value part = locationSets.set(solver, d, m);
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
