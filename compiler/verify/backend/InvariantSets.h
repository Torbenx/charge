#pragma once

#include <verify/backend/MemoryLocationSets.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>

#include <unordered_map>

namespace verify::backend {

//! A letter in the invariant prefix index
/*!
Every step into a member is preceded by a narrow, which is the step from a location to the leafs
strictly below it, and an invariant letter is the step from a location to the invariant leaf.
So the letters of a location with the member m1...mn are

    inclusive:   narrow m1 ... narrow mn
    exclusive:   narrow m1 ... narrow mn narrow
    invariant I: narrow m1 ... narrow mn I

Note: The leading narrow is requrired to correctly handle the empty path.
*/
struct InvariantLetter {
    static constexpr TheoryId NARROW_SENTINEL_THEORY = TheoryId::COUNT;
    static constexpr TheoryId INVARIANT_SENTINEL_THEORY = TheoryId(std::to_underlying(TheoryId::COUNT) + 1);
    static_assert(NARROW_SENTINEL_THEORY >= TheoryId::COUNT);
    static_assert(INVARIANT_SENTINEL_THEORY >= TheoryId::COUNT);
    static_assert(NARROW_SENTINEL_THEORY != INVARIANT_SENTINEL_THEORY);
    static_assert(INVARIANT_SENTINEL_THEORY != TheoryId::Invalid);

    static constexpr InvariantLetter invalid() { return { (Member)INVALID_VALUE }; }
    static constexpr InvariantLetter narrow() { return { Member(NARROW_SENTINEL_THEORY, limits::max) }; }
    static constexpr InvariantLetter member(Member member) { return { member }; }
    static constexpr InvariantLetter invariant(Invariant invariant) { return { Member(INVARIANT_SENTINEL_THEORY, invariant.id()) }; }

    constexpr bool isNarrow() const { return *this == narrow(); }
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

//! Either empty, narrow or an invariant
struct InvariantWordSuffix {
    static constexpr InvariantWordSuffix empty() { return { (uint32_t)limits::max }; }
    static constexpr InvariantWordSuffix narrow() { return { (uint32_t)limits::max - 1u }; }
    static constexpr InvariantWordSuffix invariant(Invariant invariant) { return { invariant.id() }; }

    constexpr bool isEmpty() const { return *this == empty(); }
    constexpr bool isNarrow() const { return *this == narrow(); }
    constexpr bool isInvariant() const { return !isEmpty() && !isNarrow(); }
    constexpr Invariant invariant() const {
        VERIFY(isInvariant());
        return Invariant { payload };
    }

    constexpr InvariantLetter toLetter() const {
        VERIFY(!isEmpty());
        if (isNarrow())
            return InvariantLetter::narrow();
        else
            return InvariantLetter::invariant(invariant());
    }

    bool operator==(const InvariantWordSuffix&) const = default;

    uint32_t payload;
};

//! Describes a word of the invariant prefix index
struct InvariantWord {
    static InvariantWord inclusive(Member member) { return { member, InvariantWordSuffix::empty() }; }
    static InvariantWord exclusive(Member member) { return { member, InvariantWordSuffix::narrow() }; }
    static InvariantWord leaf(Member member, Invariant invariant) {
        return { member, InvariantWordSuffix::invariant(invariant) };
    }

    Member member;
    InvariantWordSuffix suffix;
};

//! The prefix impl for invariant sets
struct InvariantPrefixes {
    using Letter = InvariantLetter;
    using WordKey = InvariantWord;

    static constexpr UseKind wordUse = UseKind::InvariantPrefixWord;
    static constexpr TypedReasonKind<PrefixHitData> hitReason = makeTypedReasonKind<ReasonKind::InvariantPrefixHit>();
    static constexpr InvariantLetter invalidLetter = InvariantLetter::invalid();

    static size_t hashLetter(InvariantLetter letter) { return std::bit_cast<uint32_t>(letter.payload); }

    Value watchedValue(InvariantWord word) const { return word.member; }
    void appendLetters(Solver&, InvariantWord, std::vector<InvariantLetter>& out);
    void explainLetters(Solver&, InvariantWord, ClauseBuilder&);

private:
    // Temporary buffer used inside a single function to avoid repeated allocations
    std::vector<Member> memberBuffer;
};

//! The sets of invariants described by a memory location
/*!
A leaf of this theory is the instance of one invariant at one memory location.
There are three kinds of sets:
- An inclusive location set holds the invariants of its location and those of its members
- An exclusive location set holds only the invariants of its members
- An invariant leaf set holds a single invariant

Note that these sets may not contain any elements at all, even the leaf sets.
*/
struct InvariantSets : MemoryLocationSets<InvariantSets, InvariantPrefixes> {
    static constexpr Params PARAMS = {
        .setSort = Sort::InvariantSet,
        .declarationsShareElementReason = makeTypedReasonKind<ReasonKind::InvariantDeclarationsShareElement>(),
        .pendingRewriteUse = UseKind::InvariantSetPendingContainment,
        .representativeRewriteUse = UseKind::InvariantSetRepresentative,
    };

    using Base = MemoryLocationSets<InvariantSets, InvariantPrefixes>;

    InvariantSets(Solver&);

    Value inclusiveSet(Solver&, MemoryLocation);
    Value inclusiveSet(Solver& solver, MemoryDeclaration declaration, Member member) {
        return inclusiveSet(solver, { declaration, member });
    }

    Value exclusiveSet(Solver&, MemoryLocation);
    Value exclusiveSet(Solver& solver, MemoryDeclaration declaration, Member member) {
        return exclusiveSet(solver, { declaration, member });
    }

    Value leafSet(Solver&, MemoryLocation, Invariant);
    Value leafSet(Solver& solver, MemoryDeclaration declaration, Member member, Invariant invariant) {
        return leafSet(solver, { declaration, member }, invariant);
    }

    //! Whether \p value is one of the three kinds of sets of this theory
    static constexpr bool isInvariantSet(Value value) {
        switch (value.theory()) {
        case TheoryId::InclusiveLocationInvariantSets:
        case TheoryId::ExclusiveLocationInvariantSets:
        case TheoryId::LeafInvariantSets:
            return true;
        default:
            return false;
        }
    }

    MemoryLocation locationOf(Value set) const {
        VERIFY(isInvariantSet(set));
        return setInfos[set].location;
    }

    Invariant invariantOf(Value set) const {
        VERIFY(set.theory() == TheoryId::LeafInvariantSets);
        return setInfos[set].invariant.value();
    }

    InvariantWord toWord(Value set) const;

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
        //! Holds the invariant for LeafInvariantSets
        std::optional<Invariant> invariant;
    };

    struct LeafKey {
        MemoryLocation location;
        Invariant invariant;

        bool operator==(const LeafKey&) const = default;
    };

    struct LeafHash {
        size_t operator()(const LeafKey& key) const {
            size_t hash = MemoryLocationHash()(key.location);
            hash_combine(hash, key.invariant.id());
            return hash;
        }
    };

    struct ElementState {
        std::optional<Value> leaf;
    };

    // Note: The keys of these maps could be obtained from the stored values
    using LocationSets = std::unordered_map<MemoryLocation, Value, MemoryLocationHash>;

    Value locationSet(Solver&, LocationSets&, TheoryId, MemoryLocation);

    ElementState& stateOf(ElementId element) {
        if (element.id() >= elementStates.size())
            elementStates.resize(element.id() + 1);
        return elementStates[element.id()];
    }

    std::vector<ElementState> elementStates;

    SortData<SetInfo, Sort::InvariantSet> setInfos;

    LocationSets inclusiveSets;
    LocationSets exclusiveSets;
    std::unordered_map<LeafKey, Value, LeafHash> leafSets;

    Trace<ElementId> leafTrace;
};

}
