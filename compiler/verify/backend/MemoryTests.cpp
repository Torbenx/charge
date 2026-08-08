#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

TEST(VerifyBackend, MemoryLocationSetsAreUniquePerLocation) {
    SolverImpl solver;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    MemorySet whole = solver.memorySet(d1, identity_member);
    MemorySet part = solver.memorySet(d1, m);
    MemorySet other = solver.memorySet(d2, identity_member);

    EXPECT_TRUE(whole == solver.memorySet(d1, identity_member));
    EXPECT_TRUE(part == solver.memorySet(d1, m));
    EXPECT_FALSE(whole == part);
    EXPECT_FALSE(whole == other);

    EXPECT_TRUE(solver.locationOf(part) == MemoryLocation(d1, m));
    EXPECT_TRUE(solver.locationOf(other) == MemoryLocation(d2, identity_member));

    EXPECT_EQ(solver.valueCount(TheoryId::MemoryLocationSets), 3);
}

TEST(VerifyBackend, MemoryLocationSetsLookup) {
    // Enough locations that the lookup table has to rehash its entries
    SolverImpl solver;
    std::vector<MemoryLocation> locations;
    std::vector<MemorySet> sets;
    for (int_t i = 0; i < 8; i++) {
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        for (int_t j = 0; j < 32; j++) {
            locations.push_back({ declaration, solver.newAuxMemberVariable() });
            sets.push_back(solver.memorySet(locations.back()));
        }
    }

    EXPECT_EQ(solver.valueCount(TheoryId::MemoryLocationSets), (int_t)locations.size());
    for (int_t i = 0; i < (int_t)locations.size(); i++) {
        EXPECT_TRUE(solver.memorySet(locations[i]) == sets[i]);
        EXPECT_TRUE(solver.locationOf(sets[i]) == locations[i]);
    }
}

TEST(VerifyBackend, MemoryLocationSetsOfEqualDeclarations) {
    SolverImpl solver;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    MemorySet a = solver.memorySet(d1, m);
    MemorySet b = solver.memorySet(d2, m);
    Bool setEquality = solver.equality(a, b);

    auto e = solver.newSetElement(Sort::MemorySet);
    solver.propagate();
    solver.decideTrue(e, Sets::in(a));
    solver.propagate();

    solver.decideTrue(solver.equality(d1, d2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    // The two pairs describe the same location, so the sets of that location are the same and so is
    // everything that is known about them
    EXPECT_TRUE(solver.assignedTrue(setEquality));
    EXPECT_TRUE(solver.assignedTrue(e, Sets::in(b)));
}

TEST(VerifyBackend, MemoryLocationSetsOfEqualMembers) {
    SolverImpl solver;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    MemorySet a = solver.memorySet(d, m1);
    MemorySet b = solver.memorySet(d, m2);
    Bool setEquality = solver.equality(a, b);

    auto e = solver.newSetElement(Sort::MemorySet);
    solver.propagate();
    solver.decideTrue(e, Sets::in(a));
    solver.propagate();

    // The member of a location decides it just as much as its declaration does
    solver.decideTrue(solver.equality(m1, m2));
    solver.propagate();
    EXPECT_FALSE(solver.hasConflicts());

    EXPECT_TRUE(solver.assignedTrue(setEquality));
    EXPECT_TRUE(solver.assignedTrue(e, Sets::in(b)));
}

TEST(VerifyBackend, MemoryLocationSetsNeedBothPartsOfTheLocation) {
    SolverImpl solver;
    MemoryDeclaration d1 = solver.newAuxMemoryDeclarationVariable();
    MemoryDeclaration d2 = solver.newAuxMemoryDeclarationVariable();
    Member m1 = solver.newAuxMemberVariable();
    Member m2 = solver.newAuxMemberVariable();

    MemorySet a = solver.memorySet(d1, m1);
    MemorySet b = solver.memorySet(d2, m2);
    Bool setEquality = solver.equality(a, b);
    solver.propagate();

    // One half of the location being the same says nothing about the sets
    solver.decideTrue(solver.equality(m1, m2));
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(setEquality));
    EXPECT_FALSE(solver.assignedFalse(setEquality));

    solver.decideTrue(solver.equality(d1, d2));
    solver.propagate();
    EXPECT_TRUE(solver.assignedTrue(setEquality));
}

TEST(VerifyBackend, MemoryLocationSetsInSetTheory) {
    // Location sets are ordinary sets, so the set theory reasoning applies to them
    SolverImpl solver;
    MemoryDeclaration d = solver.newAuxMemoryDeclarationVariable();
    Member m = solver.newAuxMemberVariable();

    MemorySet whole = solver.memorySet(d, identity_member);
    MemorySet part = solver.memorySet(d, m);
    Set u = solver.union_({ whole, part });

    Bool eq = solver.equality(u, whole);
    solver.propagate();
    EXPECT_FALSE(solver.assignedTrue(eq));

    // There are no rules relating a location to its members yet, so the subset relation must be
    // asserted explicitly.
    auto e = solver.newSetElement(Sort::MemorySet);
    solver.propagate();
    solver.addClause({ solver.equality(solver.subset({ part }, { whole }), solver.emptySet(Sort::MemorySet)) });
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
