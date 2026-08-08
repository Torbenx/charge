#include <verify/backend/PrefixIndex.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>

#include <gtest/gtest.h>

namespace verify::backend {

namespace {

    //! An index the words are only spelled with, so that no word is registered in the trie
    struct Fixture {
        std::unique_ptr<Solver> solverOwner = Solver::make();
        Solver& solver = *solverOwner;
        PrefixIndex prefixes;
        Invariant i1 { 0 };
        Invariant i2 { 1 };

        Member newLiteral() { return solver.newMemberLiteral(); }

        //! Decide that the member \p a is the member \p b and propagate
        void decideMembersEqual(Member a, Member b) {
            solver.decideTrue(solver.equality(a, b));
            solver.propagate();
            EXPECT_FALSE(solver.hasConflicts());
        }
    };

    //! A declaration with an element of the invariant sets, i.e. one instance of the prefix index
    struct IndexFixture {
        std::unique_ptr<Solver> solverOwner = Solver::make();
        Solver& solver = *solverOwner;
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        //! A second declaration, unrelated to \ref declaration until they are decided to be equal
        MemoryDeclaration otherDeclaration = solver.newAuxMemoryDeclarationVariable();
        SetElement element = newElement();
        Invariant i1 { 0 };
        Invariant i2 { 1 };

        Member newLiteral() { return solver.newMemberLiteral(); }

        SetElement newElement() {
            auto e = solver.newSetElement(Sort::InvariantSet);
            solver.propagate();
            return e;
        }

        InvariantSet inclusive(Member member) { return solver.inclusiveInvariantSet(declaration, member); }
        InvariantSet inclusive(std::initializer_list<Member> members) { return inclusive(solver.composeMembers(members)); }
        InvariantSet exclusive(Member member) { return solver.exclusiveInvariantSet(declaration, member); }
        InvariantSet exclusive(std::initializer_list<Member> members) { return exclusive(solver.composeMembers(members)); }
        InvariantSet path(Member member) { return solver.pathInvariantSet(declaration, member); }
        InvariantSet path(std::initializer_list<Member> members) { return path(solver.composeMembers(members)); }
        InvariantSet singleton(Member member, Invariant invariant) { return solver.invariantSingletonSet(declaration, member, invariant); }
        InvariantSet singleton(std::initializer_list<Member> members, Invariant invariant) {
            return singleton(solver.composeMembers(members), invariant);
        }

        InvariantSet otherInclusive(Member member) { return solver.inclusiveInvariantSet(otherDeclaration, member); }
        InvariantSet otherPath(Member member) { return solver.pathInvariantSet(otherDeclaration, member); }
        InvariantSet otherPath(std::initializer_list<Member> members) { return otherPath(solver.composeMembers(members)); }
        InvariantSet otherSingleton(Member member, Invariant invariant) {
            return solver.invariantSingletonSet(otherDeclaration, member, invariant);
        }

        Bool declarationEquality() { return solver.equality(declaration, otherDeclaration); }

        //! Check the index together with the theory whose rewrites it follows
        void checkInvariances() {
            solver.checkInvariances();
        }

        //! Decide that \p element is contained in \p set and propagate
        void decideIn(Set set, bool contained = true) { decideIn(element, set, contained); }
        void decideIn(SetElement e, Set set, bool contained = true) {
            solver.decideTrue(e, SetContainment(set, contained));
            solver.propagate();
            if (!solver.hasConflicts())
                checkInvariances();
        }
        void decideNotIn(Set set) { decideIn(set, false); }
        void decideNotIn(SetElement e, Set set) { decideIn(e, set, false); }

        //! Decide that the member \p a is the member \p b and propagate
        void decideMembersEqual(Member a, Member b) {
            solver.decideTrue(solver.equality(a, b));
            solver.propagate();
            if (!solver.hasConflicts())
                checkInvariances();
        }

        //! Decide that the two declarations are the same and propagate
        void decideDeclarationsEqual() {
            solver.decideTrue(declarationEquality());
            solver.propagate();
            if (!solver.hasConflicts())
                checkInvariances();
        }

        bool assignedIn(Set set) { return solver.assignedTrue(element, Sets::in(set)); }
        bool assignedNotIn(Set set) { return solver.assignedFalse(element, Sets::in(set)); }
        bool assignedEmpty(Set set) { return solver.assignedEmpty(set); }

        bool hasConflicts() const { return solver.hasConflicts(); }

        //! Resolve the pending conflicts and propagate what was learned from them
        void resolveConflicts() {
            EXPECT_TRUE(solver.hasConflicts());
            EXPECT_TRUE(solver.analyzeConflicts());
            solver.propagate();
            checkInvariances();
        }
    };

}

TEST(VerifyBackend, InvariantIndexInclusiveSetHoldsTheInvariantsOfItsLocation) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    // The inclusive set of a location holds the invariants of the location itself
    f.decideNotIn(f.inclusive(l1));
    EXPECT_FALSE(f.hasConflicts());
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.assignedNotIn(f.singleton(l1, f.i1)));
}

TEST(VerifyBackend, InvariantIndexExclusiveSetSkipsTheInvariantsOfItsLocation) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // The exclusive set of a location holds nothing of the location itself
    f.decideNotIn(f.exclusive(l1));
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    // but everything of its members. This needs an element of its own, a singleton is at one location.
    SetElement other = f.newElement();
    f.decideNotIn(other, f.exclusive(l1));
    f.decideIn(other, f.singleton({ l1, l2 }, f.i1));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexExclusiveSetIsBelowTheInclusiveOne) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // Excluding the invariants of a location excludes the ones of its members as well
    f.decideNotIn(f.inclusive(l1));
    f.decideIn(f.exclusive(l1));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();

    // And the inclusive set of a member is below the exclusive set of the location
    SetElement other = f.newElement();
    f.decideNotIn(other, f.exclusive(l1));
    f.decideIn(other, f.inclusive({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexWholeDeclaration) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    // The exclusive set of the identity location holds the invariants of everything below it. Its
    // word is the empty one, so it matches every other word of the declaration and only the
    // strictness of the hit separates the two cases here.
    f.decideNotIn(f.exclusive(identity_member));
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();

    // but not the ones of the declaration itself
    SetElement other = f.newElement();
    f.decideNotIn(other, f.exclusive(identity_member));
    f.decideIn(other, f.singleton(identity_member, f.i1));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexDistinctInvariants) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    // The singletons of two invariants at one location are unrelated
    f.decideNotIn(f.singleton(l1, f.i1));
    f.decideIn(f.singleton(l1, f.i2));
    EXPECT_FALSE(f.hasConflicts());

    // A singleton set holds nothing but its own singleton, not even the ones below its location
    SetElement other = f.newElement();
    f.decideNotIn(other, f.singleton(l1, f.i1));
    f.decideIn(other, f.singleton({ l1, l1 }, f.i1));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictByRewrite) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.exclusive(l1));
    f.decideIn(f.inclusive(v1));
    EXPECT_FALSE(f.hasConflicts());

    // v1 = l1.l2 moves the location below l1, where its invariants are the excluded ones
    Bool eq = f.solver.equality(v1, f.solver.composeMembers({ l1, l2 }));
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq));
}

TEST(VerifyBackend, InvariantIndexRewriteIsBacktracked) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.exclusive(l1));
    f.decideIn(f.singleton(v1, f.i1));
    int_t levelBeforeRewrite = f.solver.currentDecisionLevel();

    // v1 = l1.l1 puts the singleton below the excluded location
    Bool eq = f.solver.equality(v1, f.solver.composeMembers({ l1, l1 }));
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    // Reverting the rewrite by hand must leave the index without a conflict again
    f.solver.beginBacktrack(levelBeforeRewrite + 1);
    f.solver.endBacktrack();
    f.checkInvariances();
    EXPECT_FALSE(f.solver.assignedTrue(eq));

    // And the very same rewrite must conflict again when it is reapplied
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexDistinctDeclarations) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    // A prefix relation between the locations of two declarations is not a contradiction: the
    // members of l1 of the one say nothing about the invariants of l1 of the other
    f.decideNotIn(f.otherInclusive(l1));
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    // Until the two declarations turn out to be the same
    f.decideDeclarationsEqual();
    EXPECT_TRUE(f.hasConflicts());

    // The hit only holds while they are equal, so resolving it excludes that
    f.resolveConflicts();
    EXPECT_FALSE(f.hasConflicts());
    EXPECT_TRUE(f.solver.assignedFalse(f.declarationEquality()));
}

TEST(VerifyBackend, InvariantIndexDeclarationsSharingAnInvariantAreEqual) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    // A singleton belongs to one memory declaration, so a singleton of a location of two of them means that
    // those are the same declaration
    f.decideIn(f.inclusive(l1));
    EXPECT_FALSE(f.solver.assignedTrue(f.declarationEquality()));

    f.decideIn(f.otherSingleton(l1, f.i1));
    EXPECT_FALSE(f.hasConflicts());
    EXPECT_TRUE(f.solver.assignedTrue(f.declarationEquality()));
}

TEST(VerifyBackend, InvariantIndexContainmentIsDeferredUntilItsDeclarationIsJoined) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // The element is in l2 and not below l1 of the representing declaration
    f.decideIn(f.inclusive(l2));
    f.decideNotIn(f.exclusive(l1));
    EXPECT_FALSE(f.hasConflicts());

    // Being a singleton below l1 of the other declaration equates the two, which is only propagated after
    // this containment was handled. So it has to be deferred until then to be compared at all.
    f.decideIn(f.otherSingleton(f.solver.composeMembers({ l1, l2 }), f.i1));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexPathSetHoldsTheInvariantsAboveItsLocation) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // The path set of a location holds the invariants of the locations strictly above it
    f.decideNotIn(f.path({ l1, l2 }));
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.assignedNotIn(f.singleton(l1, f.i1)));

    // and holding them is all the containment in it says
    SetElement other = f.newElement();
    f.decideIn(other, f.path({ l1, l2 }));
    f.decideIn(other, f.singleton(l1, f.i1));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexPathSetSkipsTheInvariantsOfItsLocation) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // The path set of a location holds nothing of the location itself
    f.decideIn(f.path(l1));
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();

    // and nothing of the locations below it either
    SetElement other = f.newElement();
    f.decideIn(other, f.path(l1));
    f.decideIn(other, f.singleton({ l1, l2 }, f.i1));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexPathSetIsDisjointFromTheInclusiveOne) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // Nothing is both strictly above a location and at or below it. Note that both containments are
    // positive here, a conflict shape that only the path sets have.
    f.decideIn(f.path(l1));
    f.decideIn(f.inclusive(l1));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();

    // The same holds for every location below the one of the path set
    SetElement below = f.newElement();
    f.decideIn(below, f.path(l1));
    f.decideIn(below, f.inclusive({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();

    // A location above it is exactly where the invariants of a path set are, so that is no conflict
    SetElement above = f.newElement();
    f.decideIn(above, f.path({ l1, l2 }));
    f.decideIn(above, f.inclusive(l1));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexPathSetsGrowWithTheirLocation) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // A location below another one has everything on its path, plus the locations in between
    f.decideIn(f.path(l1));
    f.decideNotIn(f.path({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();

    // The other direction does not hold, those locations in between are what the higher one misses
    SetElement other = f.newElement();
    f.decideIn(other, f.path({ l1, l2 }));
    f.decideNotIn(other, f.path(l1));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexPathSetOfTheWholeDeclaration) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    InvariantSet whole = f.path(identity_member);
    InvariantSet below = f.path(l1);
    f.solver.propagate();
    EXPECT_FALSE(f.hasConflicts());

    // No location is strictly above the declaration itself, so nothing is on its path
    EXPECT_TRUE(f.assignedEmpty(whole));

    // A location below it has one, even though which invariants are on it is still open
    EXPECT_FALSE(f.assignedEmpty(below));

    // The emptiness reaches the elements on its own, so an element is known to be off that path
    // without needing a second set to place it anywhere
    EXPECT_TRUE(f.assignedNotIn(whole));
    EXPECT_FALSE(f.assignedNotIn(below));
}

TEST(VerifyBackend, InvariantIndexSingletonIsNotBelowItsOwnLocation) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // The exclusive set of a location skips the invariants of the location itself
    f.decideIn(f.singleton(l1, f.i1));
    f.decideIn(f.exclusive(l1));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();

    // and a singleton is at one location, so no set of a location below it can hold it
    SetElement other = f.newElement();
    f.decideIn(other, f.singleton(l1, f.i1));
    f.decideIn(other, f.inclusive({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexSingletonDoesNotConflictWithItself) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    // A positive singleton is added twice, once with its own word and once with the exclusive word
    // of its location. The latter is a prefix of the former, so the containment hits itself in the
    // index. The two spell the same location, so that hit is not strict and raises nothing.
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    // The same at the identity location, where both of the words are the empty one
    SetElement whole = f.newElement();
    f.decideIn(whole, f.singleton(identity_member, f.i1));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexPathSetConflictByRewrite) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.path(v1));
    EXPECT_FALSE(f.hasConflicts());

    // v1 = l1.l2 moves the location below l1, which puts the singleton on its path
    Bool eq = f.solver.equality(v1, f.solver.composeMembers({ l1, l2 }));
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq));
}

TEST(VerifyBackend, InvariantIndexPathSetRewriteIsBacktracked) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.path(v1));
    int_t levelBeforeRewrite = f.solver.currentDecisionLevel();

    // v1 = l1.l1 puts the singleton on the path of the excluded location
    Bool eq = f.solver.equality(v1, f.solver.composeMembers({ l1, l1 }));
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    // Reverting the rewrite by hand must leave the index without a conflict again
    f.solver.beginBacktrack(levelBeforeRewrite + 1);
    f.solver.endBacktrack();
    f.checkInvariances();
    EXPECT_FALSE(f.solver.assignedTrue(eq));

    // And the very same rewrite must conflict again when it is reapplied
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexPathSetDistinctDeclarations) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();

    // The path of a location of one declaration says nothing about the invariants of another
    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.otherPath({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    // Until the two declarations turn out to be the same
    f.decideDeclarationsEqual();
    EXPECT_TRUE(f.hasConflicts());

    // The hit only holds while they are equal, so resolving it excludes that
    f.resolveConflicts();
    EXPECT_FALSE(f.hasConflicts());
    EXPECT_TRUE(f.solver.assignedFalse(f.declarationEquality()));
}

TEST(VerifyBackend, InvariantIndexAVariableSuffixIsNoStrictPrefix) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    // The word of the exclusive set of l1 is a prefix of the word of the inclusive set of l1.v1,
    // but the two are only distinct locations as long as v1 is not the identity. So being an
    // invariant of l1 itself, which is outside of the exclusive set, is no contradiction.
    f.decideNotIn(f.exclusive(l1));
    f.decideIn(f.inclusive({ l1, v1 }));
    EXPECT_FALSE(f.hasConflicts());

    // The same holds for the singleton of an invariant of l1, which is where the location of a
    // positive singleton is compared as the excluding one
    SetElement other = f.newElement();
    f.decideIn(other, f.singleton(l1, f.i1));
    f.decideIn(other, f.inclusive({ l1, v1 }));
    EXPECT_FALSE(f.hasConflicts());

    // and for a path set, which is the only case where the strict prefix is on the excluded side
    SetElement onPath = f.newElement();
    f.decideIn(onPath, f.singleton(l1, f.i1));
    f.decideNotIn(onPath, f.path({ l1, v1 }));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexBacktrackTurnExcluiveIntoInclusivePrefix) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.exclusive(l1));
    f.decideIn(f.inclusive({ l1, v1 }));
    EXPECT_FALSE(f.hasConflicts());

    // v1 = l2 makes l1.v1 a location that no rewrite can bring back up to l1, and only then are the
    // invariants of l1.v1 among the ones the exclusive set of l1 holds
    int_t preEqLevel = f.solver.currentDecisionLevel();
    Bool eq = f.solver.equality(v1, l2);
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_EQ(f.solver.currentDecisionLevel(), preEqLevel);
    EXPECT_TRUE(f.solver.assignedFalse(eq));
    EXPECT_FALSE(f.assignedNotIn(f.inclusive({ l1, v1 })));
}

TEST(VerifyBackend, InvariantIndexAVariableSuffixIsNoConflictWhenItIsTheIdentity) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    // Rewriting v1 to the identity beforehand spells both words the same, which is the assignment
    // the hit of the two would have excluded
    f.solver.decideTrue(f.solver.equality(v1, identity_member));
    f.solver.propagate();
    EXPECT_FALSE(f.hasConflicts());

    f.decideNotIn(f.exclusive(l1));
    f.decideIn(f.inclusive({ l1, v1 }));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexTwoExclusionsNeverConflict) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();
    Member l3 = f.newLiteral();

    // A word is only registered once the element has a containment naming the declaration it is
    // compared in, so the exclusions need one to be in the index at all. The location of this one is
    // unrelated to l1, so it is no part of what is tested below.
    f.decideIn(f.singleton(l3, f.i1));

    // The word of an inclusive set is a prefix of the word of a path set below it, so the two hit in
    // the index. Being outside of both is no contradiction though, the element may just as well be
    // at a location neither of the two reaches.
    f.decideNotIn(f.inclusive(l1));
    f.decideNotIn(f.path({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    // The word of the exclusive set is a prefix of that path word as well
    SetElement other = f.newElement();
    f.decideIn(other, f.singleton(l3, f.i1));
    f.decideNotIn(other, f.exclusive(l1));
    f.decideNotIn(other, f.path({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsAfterRewrite) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();
    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.singleton(v1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq = f.solver.equality(l1, v1);
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq));
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsWithTheExclusionFirst) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    // The exclusion arrives before the element is known to be in any singleton, so it has to wait
    // for one to compare against instead of being dropped
    f.decideNotIn(f.singleton(v1, f.i1));
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq = f.solver.equality(l1, v1);
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq));
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsWithARewrittenKey) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    // The rewrite is on the side of the singleton the element is in this time, so the clause of the
    // match has to name that one to be about the location the two share
    f.decideIn(f.singleton(v1, f.i1));
    f.decideNotIn(f.singleton(l1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq = f.solver.equality(v1, l1);
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq));
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsOfDistinctInvariants) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    // The singletons of two invariants are distinct sets even at one location, so being in the one
    // and outside of the other stays consistent however the location is rewritten
    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.singleton(v1, f.i2));
    EXPECT_FALSE(f.hasConflicts());

    f.decideMembersEqual(l1, v1);
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsOfDistinctDeclarations) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    // A singleton belongs to one declaration, so the exclusion of another one says nothing here
    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.otherSingleton(l1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    // Until the two declarations turn out to be the same, which makes the two the same set
    f.decideDeclarationsEqual();
    EXPECT_TRUE(f.hasConflicts());

    // The match only holds while they are equal, so resolving it excludes that
    f.resolveConflicts();
    EXPECT_FALSE(f.hasConflicts());
    EXPECT_TRUE(f.solver.assignedFalse(f.declarationEquality()));
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsNeedBothHalvesOfTheLocation) {
    IndexFixture f;
    Member v1 = f.solver.newAuxMemberVariable();

    // The members of the two locations are the same from the start, only the declarations are not
    f.decideIn(f.singleton(v1, f.i1));
    f.decideNotIn(f.otherSingleton(v1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    f.decideDeclarationsEqual();
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsAtAnAlreadyEqualLocation) {
    IndexFixture f;
    Member v1 = f.solver.newAuxMemberVariable();
    Member v2 = f.solver.newAuxMemberVariable();

    // The locations are rewritten to the same one before either containment is known, so the match
    // has to be found the moment the exclusion is added rather than on a later rewrite
    f.decideMembersEqual(v1, v2);
    f.decideIn(f.singleton(v1, f.i1));
    f.decideNotIn(f.singleton(v2, f.i1));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsCompareAgainstTheKeyOnly) {
    IndexFixture f;
    Member v1 = f.solver.newAuxMemberVariable();
    Member v2 = f.solver.newAuxMemberVariable();
    Member v3 = f.solver.newAuxMemberVariable();

    // The first singleton found is the key of the element, a second one only equates the locations
    f.decideIn(f.singleton(v1, f.i1));
    f.decideIn(f.singleton(v2, f.i1));
    EXPECT_FALSE(f.hasConflicts());
    EXPECT_TRUE(f.solver.assignedTrue(f.solver.equality(v1, v2)));

    // So an exclusion that reaches the location of the second one reaches the key just as well
    f.decideNotIn(f.singleton(v3, f.i1));
    EXPECT_FALSE(f.hasConflicts());
    f.decideMembersEqual(v3, v2);
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexExcludedSingletonsWithoutAKeyNeverConflict) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    // Two locations the element is outside of say nothing about each other, even where they are the
    // same location and hold the same invariant
    f.decideNotIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.singleton(v1, f.i1));
    f.decideMembersEqual(l1, v1);
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsAreSeparatePerElement) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    // The exclusion belongs to another element than the singleton, so the two never meet
    SetElement other = f.newElement();
    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(other, f.singleton(v1, f.i1));
    f.decideMembersEqual(l1, v1);
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsAreBacktracked) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.singleton(v1, f.i1));
    int_t levelBeforeRewrite = f.solver.currentDecisionLevel();

    Bool eq = f.solver.equality(l1, v1);
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    // Reverting the rewrite by hand must leave the index without a conflict again
    f.solver.beginBacktrack(levelBeforeRewrite + 1);
    f.solver.endBacktrack();
    f.checkInvariances();
    EXPECT_FALSE(f.solver.assignedTrue(eq));

    // And the very same rewrite must be found to match anew when it is reapplied
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexExcludedSingletonsAreBacktracked) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideIn(f.singleton(l1, f.i1));
    int_t levelBeforeExclusion = f.solver.currentDecisionLevel();
    f.decideNotIn(f.singleton(v1, f.i1));

    // Reverting the exclusion has to forget the watch it registered, so the rewrite that would have
    // matched it finds nothing to compare anymore
    f.solver.beginBacktrack(levelBeforeExclusion + 1);
    f.solver.endBacktrack();
    f.checkInvariances();
    EXPECT_FALSE(f.assignedNotIn(f.singleton(v1, f.i1)));

    f.decideMembersEqual(l1, v1);
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexExcludedSingletonsOfSeveralElementsAreBacktracked) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();
    Member v2 = f.solver.newAuxMemberVariable();
    SetElement other = f.newElement();

    f.decideIn(f.singleton(l1, f.i1));
    f.decideIn(other, f.singleton(l1, f.i1));
    int_t levelBeforeExclusions = f.solver.currentDecisionLevel();

    // The exclusions of the two elements are interleaved, so reverting them has to sort them back
    // out per element rather than in the order they arrived in
    f.decideNotIn(f.singleton(v1, f.i1));
    f.decideNotIn(other, f.singleton(v2, f.i1));
    f.decideNotIn(f.singleton(v2, f.i1));
    f.decideNotIn(other, f.singleton(v1, f.i1));

    f.solver.beginBacktrack(levelBeforeExclusions + 1);
    f.solver.endBacktrack();
    f.checkInvariances();

    // None of the exclusions is left, so neither location conflicts with the shared singleton
    f.decideMembersEqual(v1, l1);
    f.decideMembersEqual(v2, l1);
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsWithSeveralExclusions) {
    IndexFixture f;
    Member l1 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();
    Member v2 = f.solver.newAuxMemberVariable();

    // Every exclusion is compared against the key on its own, so either of the two rewrites conflicts
    f.decideIn(f.singleton(l1, f.i1));
    f.decideNotIn(f.singleton(v1, f.i1));
    f.decideNotIn(f.singleton(v2, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    int_t levelBeforeRewrite = f.solver.currentDecisionLevel();
    f.decideMembersEqual(v2, l1);
    EXPECT_TRUE(f.hasConflicts());

    f.solver.beginBacktrack(levelBeforeRewrite + 1);
    f.solver.endBacktrack();
    f.checkInvariances();

    f.decideMembersEqual(v1, l1);
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexConflictingSingletonsAtTheIdentityLocation) {
    IndexFixture f;
    Member v1 = f.solver.newAuxMemberVariable();

    // The declaration itself is a location like any other, and the identity is the member spelling it
    f.decideIn(f.singleton(identity_member, f.i1));
    f.decideNotIn(f.singleton(v1, f.i1));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq = f.solver.equality(v1, identity_member);
    f.solver.decideTrue(eq);
    f.solver.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq));
}

}
