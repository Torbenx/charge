#include <verify/backend/SolverImpl.h>

#include <gtest/gtest.h>

namespace verify::backend {

namespace {

    Member newLiteral(Solver& solver) { return (Member)solver.impl().newValue(TheoryId::MemberLiterals); }

    //! A declaration with an element of the memory sets, i.e. one instance of the prefix index
    struct Fixture {
        SolverImpl solver;
        MemoryDeclaration declaration = solver.newAuxMemoryDeclarationVariable();
        //! A second declaration, unrelated to \ref declaration until they are decided to be equal
        MemoryDeclaration otherDeclaration = solver.newAuxMemoryDeclarationVariable();
        Sets::ElementId element = newElement();

        Sets::ElementId newElement() {
            auto e = solver.memorySets.newElement(solver);
            solver.sat.propagate();
            return e;
        }

        Value location(Member member) {
            return solver.memoryLocationSets.set(solver, declaration, member);
        }
        Value location(std::initializer_list<Member> members) {
            return location(solver.composeMembers(members));
        }

        Value otherLocation(Member member) {
            return solver.memoryLocationSets.set(solver, otherDeclaration, member);
        }
        Value otherLocation(std::initializer_list<Member> members) {
            return otherLocation(solver.composeMembers(members));
        }

        Bool declarationEquality() { return solver.equality(declaration, otherDeclaration); }

        //! Check the index together with the theory whose rewrites it follows
        void checkInvariances() {
            solver.memoryLocationSets.checkInvariances(solver);
            solver.members.checkInvariances(solver);
        }

        //! Decide that the member \p a is the member \p b and propagate
        void decideMembersEqual(Member a, Member b) {
            solver.decideTrue(solver.equality(a, b));
            solver.sat.propagate();
            if (!solver.sat.hasConflicts())
                checkInvariances();
        }

        //! Decide that the two declarations are the same and propagate
        void decideDeclarationsEqual() {
            solver.decideTrue(declarationEquality());
            solver.sat.propagate();
            if (!solver.sat.hasConflicts())
                checkInvariances();
        }

        //! Decide that \p element is contained in \p set and propagate
        void decideIn(Value set, bool contained = true) { decideIn(element, set, contained); }
        void decideIn(Sets::ElementId e, Value set, bool contained = true) {
            solver.memorySets.decideTrue(solver, e, Sets::Containment(set, contained));
            solver.sat.propagate();
            if (!solver.sat.hasConflicts())
                checkInvariances();
        }
        void decideNotIn(Value set) { decideIn(set, false); }
        void decideNotIn(Sets::ElementId e, Value set) { decideIn(e, set, false); }

        bool assignedIn(Value set) { return solver.memorySets.assignedTrue(solver, element, Sets::in(set)); }
        bool assignedNotIn(Value set) { return solver.memorySets.assignedFalse(solver, element, Sets::in(set)); }

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

TEST(VerifyBackend, MemoryPrefixContainmentConflict) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // An element that is not in the location l1 cannot be in the location l1.l2 below it
    f.decideNotIn(f.location(l1));
    EXPECT_FALSE(f.hasConflicts());
    f.decideIn(f.location({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.assignedNotIn(f.location({ l1, l2 })));
}

TEST(VerifyBackend, MemoryPrefixWholeDeclaration) {
    Fixture f;
    Member l1 = newLiteral(f.solver);

    // The identity is a prefix of every member, so it describes the whole declaration
    f.decideNotIn(f.location(identity_member));
    f.decideIn(f.location(l1));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixUnrelatedLocations) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // l2 occurs in l1.l2 but is not a prefix of it, so the locations are unrelated
    f.decideNotIn(f.location(l2));
    f.decideIn(f.location({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    // The containment of the wider location is not decided by the one of the narrower either
    f.decideIn(f.location({ l1, l2, l1 }));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixElementsAreIndependent) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Sets::ElementId other = f.newElement();

    // The containments of different elements say nothing about each other
    f.decideNotIn(f.element, f.location(l1));
    f.decideIn(other, f.location({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    // The same containment for the element that is excluded does conflict
    f.decideIn(f.element, f.location({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixConflictByRewrite) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.location(l1));
    f.decideIn(f.location({ v1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    // v1 = l1 turns the second location into l1.l2, which is below the first one
    Bool eq = f.solver.equality(v1, l1);
    f.solver.decideTrue(eq);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    // The equality is the last decision the conflict depends on, so it is what gets reverted, and
    // the learned clause forces it to be false. Note that Sets::refineClause() first replaces the
    // two containment literals of that clause by the emptiness of a set expression, which
    // generalizes it from this element to all of them. The clause only stays asserting because the
    // element is a witness that the expression is not empty.
    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq));
    EXPECT_TRUE(f.assignedIn(f.location({ v1, l2 })));
    EXPECT_TRUE(f.assignedNotIn(f.location(l1)));
}

TEST(VerifyBackend, MemoryPrefixConflictByIdentityRewrite) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.location({ v1, l1 }));
    f.decideIn(f.location(l1));
    EXPECT_FALSE(f.hasConflicts());

    // v1 = identity shortens the excluded location to l1, which is the included one
    Bool eq = f.solver.equality(v1, identity_member);
    f.solver.decideTrue(eq);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_FALSE(f.solver.assignedTrue(eq));
}

TEST(VerifyBackend, MemoryPrefixConflictByTransitiveRewrite) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();
    Member v2 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.location(l1));
    f.decideIn(f.location(v1));

    // v1 = l1.v2 only reaches the location once v1 itself is expanded
    Bool eq1 = f.solver.equality(v1, f.solver.composeMembers({ l1, v2 }));
    Bool eq2 = f.solver.equality(v2, l2);

    f.solver.decideTrue(eq2);
    f.solver.sat.propagate();
    EXPECT_FALSE(f.hasConflicts());

    f.solver.decideTrue(eq1);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_FALSE(f.solver.assignedTrue(eq1));
    // The rewrite that was still in place must not have been reverted with it
    EXPECT_TRUE(f.solver.assignedTrue(eq2));
}

TEST(VerifyBackend, MemoryPrefixRewriteIsBacktracked) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.location(l1));
    f.decideIn(f.location({ v1, l2 }));
    int_t levelBeforeRewrite = f.solver.currentDecisionLevel();

    Bool eq = f.solver.equality(v1, l1);
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

TEST(VerifyBackend, MemoryPrefixRewriteOfOneLevelIsBacktracked) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();
    Member v2 = f.solver.newAuxMemberVariable();

    // A word that is rebuilt at two levels, so that reverting only the second one has to leave it
    // at the normal form of the first
    f.decideIn(f.location({ v1, v2 }));
    f.decideMembersEqual(v1, l1);
    int_t levelBetweenRewrites = f.solver.currentDecisionLevel();
    f.decideMembersEqual(v2, l2);

    f.solver.sat.beginBacktrack(levelBetweenRewrites + 1);
    f.solver.sat.endBacktrack();
    f.checkInvariances();

    // The word is l1.v2 now, so the location l1 above it is still excluded by it
    f.decideNotIn(f.location(l1));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixRewriteOfAnUnregisteredWordIsBacktracked) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    // A candidate that survives the backtrack below
    f.decideNotIn(f.location({ l1, l2 }));
    int_t levelBeforeWord = f.solver.currentDecisionLevel();

    f.decideIn(f.location({ v1, l2 }));
    // The rebuild of the word is recorded above the level the word itself was registered at, so the
    // backtrack has to drop it without bringing the unregistered word back
    f.decideMembersEqual(v1, l2);
    EXPECT_FALSE(f.hasConflicts());

    f.solver.sat.beginBacktrack(levelBeforeWord + 1);
    f.solver.sat.endBacktrack();
    f.checkInvariances();

    // The candidate must still be found above a path registered afterwards
    f.decideIn(f.location({ l1, l2, l2 }));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixContainmentIsBacktracked) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    f.decideNotIn(f.location(l1));
    int_t levelBeforeInclusion = f.solver.currentDecisionLevel();
    f.decideIn(f.location({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());

    // Reverting the containment must unregister its location from the index
    f.solver.sat.beginBacktrack(levelBeforeInclusion + 1);
    f.solver.sat.endBacktrack();
    f.checkInvariances();
    EXPECT_FALSE(f.assignedIn(f.location({ l1, l2 })));

    // A different location below the excluded one must still be detected afterwards
    f.decideIn(f.location({ l1, l1 }));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixRepeatedLetters) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.location({ v1, v1 }));
    f.decideIn(f.location({ l1, l1, l1 }));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq = f.solver.equality(v1, l1);
    f.solver.decideTrue(eq);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_FALSE(f.solver.assignedTrue(eq));
}

//! The clause learned from a hit has to exclude the assignment it was learned from
/*!
Sets::refineClause() generalizes the two containment literals of the clause to the emptiness of a
set expression. That clause only stays asserting because the element is a witness that the
expression is not empty, so without SetsParams::forAllWitness the solver makes no progress and runs
into the same hit again.
*/
TEST(VerifyBackend, MemoryPrefixLearningExcludesTheConflict) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.location(l1));
    f.decideIn(f.location({ v1, l2 }));

    Bool eq1 = f.solver.equality(v1, l1);
    f.solver.decideTrue(eq1);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq1));

    // The search carries on from there and the next hit is excluded the same way
    Member v2 = f.solver.newAuxMemberVariable();
    f.decideIn(f.location({ v2, l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq2 = f.solver.equality(v2, identity_member);
    f.solver.decideTrue(eq2);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.solver.assignedFalse(eq2));
    EXPECT_TRUE(f.solver.assignedFalse(eq1));
}

TEST(VerifyBackend, MemoryPrefixEqualNormalForms) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    // A word is a prefix of itself, so two expressions that describe the same location must be
    // detected as well
    f.decideNotIn(f.location(v1));
    f.decideIn(f.location({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq = f.solver.equality(v1, f.solver.composeMembers({ l1, l2 }));
    f.solver.decideTrue(eq);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_FALSE(f.solver.assignedTrue(eq));
}

TEST(VerifyBackend, MemoryPrefixEmptyCandidateMatchesAtRoot) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    // A candidate that collapses to the empty word ends in the element root, where every path of
    // the element has its first occurrence
    f.decideNotIn(f.location(v1));
    f.decideIn(f.location(l1));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq = f.solver.equality(v1, identity_member);
    f.solver.decideTrue(eq);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_FALSE(f.solver.assignedTrue(eq));
}

TEST(VerifyBackend, MemoryPrefixEmptyPathMatchesEmptyCandidate) {
    Fixture f;
    Member v1 = f.solver.newAuxMemberVariable();

    // Both words end in the element root, so the root has to carry an occurrence of the path too
    f.decideNotIn(f.location(v1));
    f.decideIn(f.location(identity_member));
    EXPECT_FALSE(f.hasConflicts());

    Bool eq = f.solver.equality(v1, identity_member);
    f.solver.decideTrue(eq);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixPathBeforeCandidate) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // The reverse registration order of MemoryPrefixContainmentConflict: the path occupies the
    // nodes first and the candidate has to find it when it attaches
    f.decideIn(f.location({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());
    f.decideNotIn(f.location(l1));
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.assignedIn(f.location(l1)));
}

TEST(VerifyBackend, MemoryPrefixSeveralHitsForOnePath) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member l3 = newLiteral(f.solver);

    // Two candidates on the path of the same location, so attaching it hits both of them and
    // reports a separate conflict for each
    f.decideNotIn(f.location(l1));
    f.decideNotIn(f.location({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    f.decideIn(f.location({ l1, l2, l3 }));
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_TRUE(f.assignedNotIn(f.location({ l1, l2, l3 })));
}

TEST(VerifyBackend, MemoryPrefixSeveralHitsForOneCandidate) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member l3 = newLiteral(f.solver);

    // The mirrored case: several paths below the node a single candidate attaches to
    f.decideIn(f.location({ l1, l2 }));
    f.decideIn(f.location({ l1, l3 }));
    f.decideIn(f.location({ l1, l2, l3 }));
    EXPECT_FALSE(f.hasConflicts());

    f.decideNotIn(f.location(l1));
    EXPECT_TRUE(f.hasConflicts());
    f.resolveConflicts();
}

TEST(VerifyBackend, MemoryPrefixSharedNodesAreDetachedCorrectly) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member l3 = newLiteral(f.solver);

    // Several paths crowding the same nodes, so that unregistering them exercises the swap removal
    // from the occurrence lists of the shared nodes
    f.decideIn(f.location({ l1 }));
    int_t level = f.solver.currentDecisionLevel();
    f.decideIn(f.location({ l1, l2 }));
    f.decideIn(f.location({ l1, l2, l3 }));
    f.decideIn(f.location({ l1, l3 }));
    f.decideIn(f.location({ l1, l2, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    // Drop the last four again, leaving the first one on the shared nodes
    f.solver.sat.beginBacktrack(level + 1);
    f.solver.sat.endBacktrack();
    f.checkInvariances();
    EXPECT_TRUE(f.assignedIn(f.location({ l1 })));
    EXPECT_FALSE(f.assignedIn(f.location({ l1, l2 })));

    // The survivor must still be found below a candidate
    f.decideNotIn(f.location(identity_member));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixBothWordsRewrittenAtOnce) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();

    // Both words watch v1, so a single equality makes both of them dirty and the first one is
    // reattached while the second one still sits at its old node
    f.decideNotIn(f.location({ l1, v1 }));
    f.decideIn(f.location({ v1, l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    // v1 = l1 turns the words into l1.l1 and l1.l1.l2
    Bool eq = f.solver.equality(v1, l1);
    f.solver.decideTrue(eq);
    f.solver.sat.propagate();
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_FALSE(f.solver.assignedTrue(eq));
}

TEST(VerifyBackend, MemoryPrefixWordWatchingSeveralVariables) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member v1 = f.solver.newAuxMemberVariable();
    Member v2 = f.solver.newAuxMemberVariable();

    f.decideNotIn(f.location({ v1, v2, l1 }));
    f.decideIn(f.location(l1));
    EXPECT_FALSE(f.hasConflicts());

    // Both variables become the identity in one go, so the word is notified for two of the
    // variables it watches while its normal form only changes once
    f.decideMembersEqual(f.solver.composeMembers({ v1, v2 }), identity_member);
    EXPECT_TRUE(f.solver.members.rewrite(v1).empty());
    EXPECT_TRUE(f.solver.members.rewrite(v2).empty());
    EXPECT_TRUE(f.hasConflicts());

    f.resolveConflicts();
    EXPECT_FALSE(f.solver.assignedTrue(f.solver.equality(f.solver.composeMembers({ v1, v2 }), identity_member)));
}

TEST(VerifyBackend, MemoryPrefixElementRootsGrowLate) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // An element that only shows up after the trie already holds the words of another one
    f.decideNotIn(f.element, f.location(l1));
    Sets::ElementId other = f.newElement();
    f.decideIn(other, f.location({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    f.decideNotIn(other, f.location(l1));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixEmptySetIsACandidate) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // An empty location set is a containment of the forall element, which is distributed to the
    // real elements and becomes a candidate for them
    f.solver.addClause({ f.solver.equality(f.location(l1), f.solver.memorySets.emptySet()) });
    f.solver.sat.propagate();
    EXPECT_FALSE(f.hasConflicts());
    f.checkInvariances();

    f.decideIn(f.location({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixDistinctDeclarations) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // A prefix relation between the locations of two declarations is not a contradiction: l1 of the
    // one says nothing about l1.l2 of the other
    f.decideNotIn(f.otherLocation(l1));
    f.decideIn(f.location({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    // And neither is it the other way around. This needs an element of its own, because an element
    // of a location of both declarations would make them equal.
    Sets::ElementId other = f.newElement();
    f.decideNotIn(other, f.location(l2));
    f.decideIn(other, f.otherLocation({ l2, l1 }));
    EXPECT_FALSE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixDeclarationsJoinedAtTheRepresentative) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // The containing location is the representative of the element, so joining the declarations
    // moves its declaration and the excluded location is only reported by the use of the
    // representative
    f.decideNotIn(f.location(l1));
    f.decideIn(f.otherLocation({ l1, l2 }));
    EXPECT_FALSE(f.hasConflicts());

    f.decideDeclarationsEqual();
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixDeclarationsJoinedAtTheContainment) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // Here the representative keeps its declaration, so the excluded location is the one that is
    // moved and it is reported by the use of its own pending containment
    f.decideIn(f.location({ l1, l2 }));
    f.decideNotIn(f.otherLocation(l1));
    EXPECT_FALSE(f.hasConflicts());

    f.decideDeclarationsEqual();
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixContainmentIsDeferredUntilItsDeclarationIsJoined) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);
    Member l3 = newLiteral(f.solver);

    // The element is in l3 and not in l1 of the representing declaration
    f.decideIn(f.location(l3));
    f.decideNotIn(f.location(l1));
    EXPECT_FALSE(f.hasConflicts());

    // Being in l1.l2 of the other declaration equates the two, which is only propagated after this
    // containment was handled. So it has to be deferred until then to be compared with l1 at all.
    f.decideIn(f.otherLocation({ l1, l2 }));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixDeclarationsSharingAnElementAreEqual) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    // Distinct declarations describe distinct memory, so an element of a location of each of them
    // means they are the same declaration
    f.decideIn(f.location(l1));
    EXPECT_FALSE(f.solver.assignedTrue(f.declarationEquality()));

    f.decideIn(f.otherLocation(l2));
    EXPECT_FALSE(f.hasConflicts());
    EXPECT_TRUE(f.solver.assignedTrue(f.declarationEquality()));
}

TEST(VerifyBackend, MemoryPrefixSharedElementConflictsWithDisequalDeclarations) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    f.solver.addClause({ !f.declarationEquality() });
    f.solver.sat.propagate();

    // The declarations cannot be equal, so no element can be in a location of both
    f.decideIn(f.location(l1));
    f.decideIn(f.otherLocation(l2));
    EXPECT_TRUE(f.hasConflicts());
}

TEST(VerifyBackend, MemoryPrefixDeclarationEqualityIsPartOfTheConflict) {
    Fixture f;
    Member l1 = newLiteral(f.solver);
    Member l2 = newLiteral(f.solver);

    f.decideIn(f.location({ l1, l2 }));
    f.decideNotIn(f.otherLocation(l1));
    f.decideDeclarationsEqual();
    EXPECT_TRUE(f.hasConflicts());

    // The hit only holds while the declarations are equal, so resolving it excludes that and the
    // words of the joined declaration are gone again
    f.resolveConflicts();
    EXPECT_FALSE(f.hasConflicts());
    EXPECT_TRUE(f.solver.assignedFalse(f.declarationEquality()));
}

}
