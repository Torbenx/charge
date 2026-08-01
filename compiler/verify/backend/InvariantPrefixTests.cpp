#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

namespace {

    using Letters = std::vector<InvariantLetter>;

    //! An index the words are only spelled with, so that no word is registered in the trie
    struct Fixture {
        SolverImpl solver;
        PrefixIndex<InvariantPrefixes> prefixes;
        Invariant i1 { 0 };
        Invariant i2 { 1 };

        Member newLiteral() { return solver.newMemberLiteral(); }

        Letters spell(InvariantWord word) {
            Letters letters;
            prefixes.impl.appendLetters(solver, word, letters);
            return letters;
        }

        //! Whether the set of \p superset holds every singleton the set of \p subset holds
        /*!
        The words are ordered by the prefix relation, which is what the index detects hits with.
        */
        bool holdsAllOf(InvariantWord superset, InvariantWord subset) {
            Letters prefix = spell(superset);
            Letters word = spell(subset);
            return prefix.size() <= word.size() && std::equal(prefix.begin(), prefix.end(), word.begin());
        }

        //! Decide that the member \p a is the member \p b and propagate
        void decideMembersEqual(Member a, Member b) {
            solver.decideTrue(solver.equality(a, b));
            solver.sat.propagate();
            EXPECT_FALSE(solver.sat.hasConflicts());
        }
    };

    //! A declaration with an element of the invariant sets, i.e. one instance of the prefix index
    struct IndexFixture {
        SolverImpl solver;
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        //! A second declaration, unrelated to \ref declaration until they are decided to be equal
        MemoryDeclaration otherDeclaration = solver.newAuxMemoryDeclarationVariable();
        Sets::ElementId element = newElement();
        Invariant i1 { 0 };
        Invariant i2 { 1 };

        Member newLiteral() { return solver.newMemberLiteral(); }

        Sets::ElementId newElement() {
            auto e = solver.invariantSetsBaseTheory.newElement(solver);
            solver.sat.propagate();
            return e;
        }

        InvariantSet inclusive(Member member) { return solver.invariantSets.inclusiveSet(solver, declaration, member); }
        InvariantSet inclusive(std::initializer_list<Member> members) { return inclusive(solver.composeMembers(members)); }
        InvariantSet exclusive(Member member) { return solver.invariantSets.exclusiveSet(solver, declaration, member); }
        InvariantSet exclusive(std::initializer_list<Member> members) { return exclusive(solver.composeMembers(members)); }
        InvariantSet singleton(Member member, Invariant invariant) { return solver.invariantSets.singletonSet(solver, declaration, member, invariant); }
        InvariantSet singleton(std::initializer_list<Member> members, Invariant invariant) {
            return singleton(solver.composeMembers(members), invariant);
        }

        InvariantSet otherInclusive(Member member) { return solver.invariantSets.inclusiveSet(solver, otherDeclaration, member); }
        InvariantSet otherSingleton(Member member, Invariant invariant) {
            return solver.invariantSets.singletonSet(solver, otherDeclaration, member, invariant);
        }

        Bool declarationEquality() { return solver.equality(declaration, otherDeclaration); }

        //! Check the index together with the theory whose rewrites it follows
        void checkInvariances() {
            solver.invariantSets.checkInvariances(solver);
            solver.members.checkInvariances(solver);
        }

        //! Decide that \p element is contained in \p set and propagate
        void decideIn(Set set, bool contained = true) { decideIn(element, set, contained); }
        void decideIn(Sets::ElementId e, Set set, bool contained = true) {
            solver.invariantSetsBaseTheory.decideTrue(solver, e, Sets::Containment(set, contained));
            solver.sat.propagate();
            if (!solver.sat.hasConflicts())
                checkInvariances();
        }
        void decideNotIn(Set set) { decideIn(set, false); }
        void decideNotIn(Sets::ElementId e, Set set) { decideIn(e, set, false); }

        //! Decide that the two declarations are the same and propagate
        void decideDeclarationsEqual() {
            solver.decideTrue(declarationEquality());
            solver.sat.propagate();
            if (!solver.sat.hasConflicts())
                checkInvariances();
        }

        bool assignedIn(Set set) { return solver.invariantSetsBaseTheory.assignedTrue(solver, element, Sets::in(set)); }
        bool assignedNotIn(Set set) { return solver.invariantSetsBaseTheory.assignedFalse(solver, element, Sets::in(set)); }

        bool hasConflicts() const { return solver.sat.hasConflicts(); }

        //! Resolve the pending conflicts and propagate what was learned from them
        void resolveConflicts() {
            EXPECT_TRUE(solver.sat.hasConflicts());
            EXPECT_TRUE(solver.sat.analyzeConflicts());
            solver.sat.propagate();
            checkInvariances();
        }
    };

}

TEST(VerifyBackend, InvariantWordsAreSpelledWithNarrows) {
    Fixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();
    Member location = f.solver.composeMembers({ l1, l2 });

    InvariantLetter narrow = InvariantLetter::narrow();
    InvariantLetter first = InvariantLetter::member(l1);
    InvariantLetter second = InvariantLetter::member(l2);

    // Every step into a member is preceded by a narrow, and the suffix tells the kinds apart
    EXPECT_EQ(f.spell(InvariantWord::inclusive(location)), (Letters { narrow, first, narrow, second }));
    EXPECT_EQ(f.spell(InvariantWord::exclusive(location)), (Letters { narrow, first, narrow, second, narrow }));
    EXPECT_EQ(f.spell(InvariantWord::singleton(location, f.i1)),
        (Letters { narrow, first, narrow, second, InvariantLetter::invariant(f.i1) }));

    // The whole declaration is the identity location, so its inclusive set is the empty word
    EXPECT_EQ(f.spell(InvariantWord::inclusive(identity_member)), (Letters {}));
    EXPECT_EQ(f.spell(InvariantWord::exclusive(identity_member)), (Letters { narrow }));
    EXPECT_EQ(f.spell(InvariantWord::singleton(identity_member, f.i1)), (Letters { InvariantLetter::invariant(f.i1) }));
}

TEST(VerifyBackend, InvariantWordsOfOneLocation) {
    Fixture f;
    Member l1 = f.newLiteral();

    // The inclusive set holds the invariants of the location itself and the ones below it
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::inclusive(l1), InvariantWord::singleton(l1, f.i1)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::inclusive(l1), InvariantWord::exclusive(l1)));

    // The exclusive set holds neither the invariants of the location nor its inclusive set
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::singleton(l1, f.i1)));
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::inclusive(l1)));

    // The singletons of two invariants at one location are distinct, and every set holds itself
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::singleton(l1, f.i1), InvariantWord::singleton(l1, f.i2)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::singleton(l1, f.i1), InvariantWord::singleton(l1, f.i1)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::inclusive(l1), InvariantWord::inclusive(l1)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::exclusive(l1)));
}

TEST(VerifyBackend, InvariantWordsOfNestedLocations) {
    Fixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();
    Member below = f.solver.composeMembers({ l1, l2 });

    // A member of a location is below it, so the exclusive set holds everything of that member
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::inclusive(below)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::exclusive(below)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::singleton(below, f.i1)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::inclusive(l1), InvariantWord::inclusive(below)));

    // And nothing of the location above it
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::inclusive(below), InvariantWord::inclusive(l1)));
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::inclusive(below), InvariantWord::singleton(l1, f.i1)));

    // The sets of two members of the same location are unrelated
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::inclusive(l1), InvariantWord::inclusive(l2)));
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::inclusive(l2)));
}

TEST(VerifyBackend, InvariantWordsOfTheWholeDeclaration) {
    Fixture f;
    Member l1 = f.newLiteral();

    // The inclusive set of the identity location holds everything of the declaration
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::inclusive(identity_member), InvariantWord::inclusive(l1)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::inclusive(identity_member), InvariantWord::singleton(l1, f.i1)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::inclusive(identity_member), InvariantWord::singleton(identity_member, f.i1)));

    // The exclusive one holds all of that except the invariants of the identity location itself. This is
    // what the leading narrow of every word is needed for.
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::exclusive(identity_member), InvariantWord::inclusive(l1)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::exclusive(identity_member), InvariantWord::exclusive(l1)));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::exclusive(identity_member), InvariantWord::singleton(l1, f.i1)));
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::exclusive(identity_member), InvariantWord::singleton(identity_member, f.i1)));
}

TEST(VerifyBackend, InvariantWordsFollowRewrites) {
    Fixture f;
    Member l1 = f.newLiteral();
    Member l2 = f.newLiteral();
    Member v1 = f.solver.newAuxMemberVariable();

    // The words are spelled from the normal form of their member, so a location that is not related
    // to another one yet
    EXPECT_FALSE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::inclusive(v1)));

    // becomes one below it once the variable is rewritten
    f.decideMembersEqual(v1, f.solver.composeMembers({ l1, l2 }));
    EXPECT_TRUE(f.holdsAllOf(InvariantWord::exclusive(l1), InvariantWord::inclusive(v1)));
    EXPECT_EQ(f.spell(InvariantWord::inclusive(v1)), f.spell(InvariantWord::inclusive(f.solver.composeMembers({ l1, l2 }))));
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
    Sets::ElementId other = f.newElement();
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
    Sets::ElementId other = f.newElement();
    f.decideNotIn(other, f.exclusive(l1));
    f.decideIn(other, f.inclusive({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, InvariantIndexWholeDeclaration) {
    IndexFixture f;
    Member l1 = f.newLiteral();

    // The exclusive set of the identity location holds the invariants of everything below it, which is
    // what the leading narrow of every word is needed for
    f.decideNotIn(f.exclusive(identity_member));
    f.decideIn(f.singleton(l1, f.i1));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();

    // but not the ones of the declaration itself
    Sets::ElementId other = f.newElement();
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
    Sets::ElementId other = f.newElement();
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
    f.solver.sat.propagate();
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
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    // Reverting the rewrite by hand must leave the index without a conflict again
    f.solver.sat.beginBacktrack(levelBeforeRewrite + 1);
    f.solver.sat.endBacktrack();
    f.checkInvariances();
    EXPECT_FALSE(f.solver.assignedTrue(eq));

    // And the very same rewrite must conflict again when it is reapplied
    f.solver.decideTrue(eq);
    f.solver.sat.propagate();
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

}
