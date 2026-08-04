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
struct InvariantSets : MemoryLocationSets<InvariantSets> {
    static constexpr Params PARAMS = {
        .setSort = Sort::InvariantSet,
        .declarationsShareElementReason = makeTypedReasonKind<ReasonKind::InvariantDeclarationsShareElement>(),
        .pendingRewriteUse = UseKind::InvariantSetPendingContainment,
        .representativeRewriteUse = UseKind::InvariantSetRepresentative,
        .prefixParams = {
            .hitReason = makeTypedReasonKind<ReasonKind::InvariantPrefixHit>(),
            .wordUse = UseKind::InvariantPrefixWord,
        },
    };

    using Base = MemoryLocationSets<InvariantSets>;
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
        switch (set.theory()) {
        case TheoryId::InclusiveLocationInvariantSets:
            return inclusiveInfos[set].location;
        case TheoryId::ExclusiveLocationInvariantSets:
            return exclusiveInfos[set].location;
        case TheoryId::PathInvariantSets:
            return pathInfos[set].location;
        case TheoryId::InvariantSingletonSets:
            return singletonInfos[set].location;
        default:
            VERIFY_NOT_REACHED();
        }
    }

    Invariant invariantOf(InvariantSet set) const {
        VERIFY(set.theory() == TheoryId::InvariantSingletonSets);
        return singletonInfos[set].invariant;
    }

    void addWords(Solver&, PrefixIndex&, ElementId, Containment);

    void propagateContainment(Solver&, ElementId, Containment);

    bool testReason(Solver&, Bool, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);

    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);

    void checkInvariances(Solver&);

private:
    struct LocationSetInfo {
        LocationSetInfo() = default;
        MemoryLocation location { MemoryDeclaration(INVALID_VALUE) };
    };

    struct SingletonSetInfo {
        SingletonSetInfo() = default;
        MemoryLocation location { MemoryDeclaration(INVALID_VALUE) };
        Invariant invariant { limits::max };
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

    ElementState& stateOf(ElementId element) {
        if (element.id() >= elementStates.size())
            elementStates.resize(element.id() + 1);
        return elementStates[element.id()];
    }

    std::vector<ElementState> elementStates;

    TheoryData<LocationSetInfo, TheoryId::InclusiveLocationInvariantSets> inclusiveInfos;
    TheoryData<LocationSetInfo, TheoryId::ExclusiveLocationInvariantSets> exclusiveInfos;
    TheoryData<LocationSetInfo, TheoryId::PathInvariantSets> pathInfos;
    TheoryData<SingletonSetInfo, TheoryId::InvariantSingletonSets> singletonInfos;

    LocationSets inclusiveSets;
    LocationSets exclusiveSets;
    LocationSets pathSets;
    std::unordered_map<SingletonKey, InvariantSet, SingletonHash> singletonSets;

    Trace<ElementId> singletonTrace;
};

}
