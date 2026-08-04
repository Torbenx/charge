#pragma once

#include <verify/backend/MemoryLocationSets.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>

#include <unordered_map>

namespace verify::backend {

//! A letter in the invariant prefix index
/*!
A word is spelled with the members of its location, and an invariant letter is the step from a
location to the invariant singleton. So the letters of a location with the members m1...mn are

    inclusive:   m1 ... mn
    exclusive:   m1 ... mn
    invariant I: m1 ... mn I

The inclusive and the exclusive set of a location are spelled the same, what tells the two apart is
the kind of their word. An exclusive word holds only the invariants strictly below its location, so
its hits are conflicts only where the path stays strictly longer under every rewrite, which is what
the strictPrefix flag of InvariantPrefixes::raisesConflict() decides.
*/
struct InvariantLetter {
    static constexpr TheoryId INVARIANT_SENTINEL_THEORY = TheoryId::COUNT;
    static_assert(INVARIANT_SENTINEL_THEORY >= TheoryId::COUNT);
    static_assert(INVARIANT_SENTINEL_THEORY != TheoryId::Invalid);

    static constexpr InvariantLetter invalid() { return { (Member)INVALID_VALUE }; }
    static constexpr InvariantLetter member(Member member) { return { member }; }
    static constexpr InvariantLetter invariant(Invariant invariant) { return { Member(INVARIANT_SENTINEL_THEORY, invariant.id()) }; }

    constexpr bool isInvariant() const { return payload.theory() == INVARIANT_SENTINEL_THEORY; }
    constexpr bool isMember() const { return payload.theory() < TheoryId::COUNT; }
    constexpr Invariant invariant() const {
        VERIFY(isInvariant());
        return Invariant { payload.id() };
    }
    constexpr Member member() const {
        VERIFY(isMember());
        return payload;
    }

    bool operator==(const InvariantLetter&) const = default;

    Member payload;
};

//! Which of the three kinds of set a word describes
/*!
Only the invariant kind contributes a letter, the other two are spelled from their member alone and
are told apart by this kind at the hits of the index.
*/
struct InvariantWordKind {
    static constexpr InvariantWordKind inclusive() { return { (uint32_t)limits::max }; }
    static constexpr InvariantWordKind exclusive() { return { (uint32_t)limits::max - 1u }; }
    static constexpr InvariantWordKind invariant(Invariant invariant) { return { invariant.id() }; }

    constexpr bool isInclusive() const { return *this == inclusive(); }
    constexpr bool isExclusive() const { return *this == exclusive(); }
    constexpr bool isInvariant() const { return !isInclusive() && !isExclusive(); }
    constexpr Invariant invariant() const {
        VERIFY(isInvariant());
        return Invariant { payload };
    }

    bool operator==(const InvariantWordKind&) const = default;

    uint32_t payload;
};

//! Describes a word of the invariant prefix index
struct InvariantWord {
    static InvariantWord inclusive(Member member) { return { member, InvariantWordKind::inclusive() }; }
    static InvariantWord exclusive(Member member) { return { member, InvariantWordKind::exclusive() }; }
    static InvariantWord singleton(Member member, Invariant invariant) {
        return { member, InvariantWordKind::invariant(invariant) };
    }

    Member member;
    InvariantWordKind kind;
};

//! The prefix impl for invariant sets
struct InvariantPrefixes {
    using Letter = InvariantLetter;
    using WordKey = InvariantWord;

    static constexpr UseKind wordUse = UseKind::InvariantPrefixWord;
    static constexpr TypedReasonKind<PrefixHitData> hitReason = makeTypedReasonKind<ReasonKind::InvariantPrefixHit>();
    static constexpr InvariantLetter invalidLetter = InvariantLetter::invalid();

    static size_t hashLetter(InvariantLetter letter) { return std::bit_cast<uint32_t>(letter.payload); }

    //! Only a literal member is stable, a variable one can still be rewritten to the identity
    /*!
    An invariant letter must not count here even though no rewrite ever removes it: the strict
    prefix it would make is one of an invariant, not one of a location, and the conflicts gated by
    it need the location of the path to be strictly below the one of the prefix. Counting it would
    for example put the singleton of an invariant of l1.v1.I strictly below l1 while v1 may still
    turn out to be the identity.
    */
    static bool letterStable(InvariantLetter letter) { return letter.isMember() && letter.member().literal(); }

    Value watchedValue(InvariantWord word) const { return word.member; }
    void appendLetters(Solver&, InvariantWord, std::vector<InvariantLetter>& out);
    void explainLetters(Solver&, InvariantWord, ClauseBuilder&);

    bool raisesConflict(PrefixHitSide<InvariantWord> prefix, PrefixHitSide<InvariantWord> path, bool strictPrefix) const;

private:
    // Temporary buffer used inside a single function to avoid repeated allocations
    std::vector<Member> memberBuffer;
};

//! The sets of invariants described by a memory location
/*!
There are four kinds of sets:
- An inclusive location set holds the invariants of its location and those of its members
- An exclusive location set holds only the invariants of its members
- A path location set holds the invariants of the locations strictly above its own
- An invariant singleton set holds a single invariant

The first two grow downwards with their location, the path sets grow upwards. The path set of
the whole declaration is empty.

Note that the non-singleton sets may not contain any elements at all.
*/
struct InvariantSets : MemoryLocationSets<InvariantSets, InvariantPrefixes> {
    static constexpr Params PARAMS = {
        .setSort = Sort::InvariantSet,
        .declarationsShareElementReason = makeTypedReasonKind<ReasonKind::InvariantDeclarationsShareElement>(),
        .pendingRewriteUse = UseKind::InvariantSetPendingContainment,
        .representativeRewriteUse = UseKind::InvariantSetRepresentative,
    };

    using Base = MemoryLocationSets<InvariantSets, InvariantPrefixes>;
    using SetHandle = InvariantSet;

    InvariantSets(Solver&);

    InvariantSet inclusiveSet(Solver&, MemoryLocation);
    InvariantSet inclusiveSet(Solver& solver, MemoryDeclaration declaration, Member member) {
        return inclusiveSet(solver, { declaration, member });
    }

    InvariantSet exclusiveSet(Solver&, MemoryLocation);
    InvariantSet exclusiveSet(Solver& solver, MemoryDeclaration declaration, Member member) {
        return exclusiveSet(solver, { declaration, member });
    }

    InvariantSet pathSet(Solver&, MemoryLocation);
    InvariantSet pathSet(Solver& solver, MemoryDeclaration declaration, Member member) {
        return pathSet(solver, { declaration, member });
    }

    InvariantSet singletonSet(Solver&, MemoryLocation, Invariant);
    InvariantSet singletonSet(Solver& solver, MemoryDeclaration declaration, Member member, Invariant invariant) {
        return singletonSet(solver, { declaration, member }, invariant);
    }

    //! Whether \p value is one of the three kinds of sets of this theory
    static constexpr bool isInvariantSet(Value value) {
        switch (value.theory()) {
        case TheoryId::InclusiveLocationInvariantSets:
        case TheoryId::ExclusiveLocationInvariantSets:
        case TheoryId::PathInvariantSets:
        case TheoryId::InvariantSingletonSets:
            return true;
        default:
            return false;
        }
    }

    MemoryLocation locationOf(InvariantSet set) const {
        VERIFY(isInvariantSet(set));
        return setInfos[set].location;
    }

    Invariant invariantOf(InvariantSet set) const {
        VERIFY(set.theory() == TheoryId::InvariantSingletonSets);
        return setInfos[set].invariant.value();
    }

    InvariantWord toWord(InvariantSet set) const;
    void addWords(Solver&, Prefixes&, ElementId, Containment);

    void propagateContainment(Solver&, ElementId, Containment);

    bool testReason(Solver&, Bool, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);

    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);

    void checkInvariances(Solver&);

private:
    struct SetInfo {
        SetInfo() = default;
        MemoryLocation location { MemoryDeclaration(INVALID_VALUE) };
        //! Holds the invariant for InvariantSingletonSets
        std::optional<Invariant> invariant;
    };

    struct SingletonKey {
        MemoryLocation location;
        Invariant invariant;

        bool operator==(const SingletonKey&) const = default;
    };

    struct SingletonHash {
        size_t operator()(const SingletonKey& key) const {
            size_t hash = MemoryLocationHash()(key.location);
            hash_combine(hash, key.invariant.id());
            return hash;
        }
    };

    struct ElementState {
        std::optional<InvariantSet> singleton;
    };

    // Note: The keys of these maps could be obtained from the stored values
    using LocationSets = std::unordered_map<MemoryLocation, InvariantSet, MemoryLocationHash>;

    InvariantSet locationSet(Solver&, LocationSets&, TheoryId, MemoryLocation);

    ElementState& stateOf(ElementId element) {
        if (element.id() >= elementStates.size())
            elementStates.resize(element.id() + 1);
        return elementStates[element.id()];
    }

    std::vector<ElementState> elementStates;

    SortData<SetInfo, Sort::InvariantSet> setInfos;

    LocationSets inclusiveSets;
    LocationSets exclusiveSets;
    LocationSets pathSets;
    std::unordered_map<SingletonKey, InvariantSet, SingletonHash> singletonSets;

    Trace<ElementId> singletonTrace;
};

}
