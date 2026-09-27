#pragma once

#include <verify/backend/MemoryLocationSets.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>

#include <unordered_map>

namespace verify::backend {

//! The sets of invariants described by a memory location
/*!
There are four kinds of sets:
- An inclusive location set holds the invariants of its location and those of its members
- An exclusive location set holds only the invariants of its members
- A path location set holds the invariants of the locations strictly above its own
- An exact set holds its invariant at its location if the type of the location has
  that invariant, and is empty otherwise.

The first two grow downwards with their location, the path sets grow upwards. The path set of
the whole declaration is empty.

Note that any of the sets can be empty.
*/
struct InvariantSets : MemoryLocationSets<InvariantSets> {
    static constexpr Params PARAMS = {
        .setSort = Sort::InvariantSet,
        .declarationsShareElementReason = makeTypedReasonKind<ReasonKind::InvariantDeclarationsShareElement>(),
        .prefixParams = {
            .hitReason = makeTypedReasonKind<ReasonKind::InvariantPrefixHit>(),
            .wordUse = UseKind::InvariantPrefixWord,
        },
        .watchesParams = {
            .keyUse = UseKind::InvariantSetRepresentative,
            .watchUse = UseKind::InvariantSetPendingContainment,
        },
    };

    using Base = MemoryLocationSets<InvariantSets>;
    using SetHandle = InvariantSet;

    InvariantSets(Solver&);

    InvariantSet inclusiveSet(Solver&, MemoryLocation);
    InvariantSet exclusiveSet(Solver&, MemoryLocation);
    InvariantSet pathSet(Solver&, MemoryLocation);
    InvariantSet exactSet(Solver&, MemoryLocation, Invariant);

    //! Whether \p value is one of the kinds of this theory
    static constexpr bool isInvariantSet(Value value) {
        switch (value.theory()) {
        case TheoryId::InclusiveLocationInvariantSets:
        case TheoryId::ExclusiveLocationInvariantSets:
        case TheoryId::PathInvariantSets:
        case TheoryId::ExactInvariantSets:
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
        case TheoryId::ExactInvariantSets:
            return exactInfos[set].location;
        default:
            VERIFY_NOT_REACHED();
        }
    }

    Invariant invariantOf(InvariantSet set) const {
        VERIFY(set.theory() == TheoryId::ExactInvariantSets);
        return exactInfos[set].invariant;
    }

    void addWords(Solver&, PrefixIndex&, SetElement, SetContainment);

    void propagateRewrite(Solver&, Use);
    void propagateContainment(Solver&, SetElement, SetContainment);

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

    struct ExactSetInfo {
        ExactSetInfo() = default;
        MemoryLocation location { MemoryDeclaration(INVALID_VALUE) };
        Invariant invariant { limits::max };
    };

    struct ExactSetKey {
        MemoryLocation location;
        Invariant invariant;

        bool operator==(const ExactSetKey&) const = default;
    };

    struct ExactSetHash {
        size_t operator()(const ExactSetKey& key) const {
            size_t hash = MemoryLocationHash()(key.location);
            hash_combine(hash, key.invariant.id());
            return hash;
        }
    };

    struct ExactSetIndex : KeyWatches<ExactSetIndex, InvariantSet, InvariantSet> {
        static constexpr KeyWatchesParams PARAMS = {
            .keyUse = UseKind::InvariantExactKeyUse,
            .watchUse = UseKind::InvariantExactWatchUse,
        };

        InvariantSets& invariantSets();
        void addValueUses(Solver&, SetElement, InvariantSet, Use);
        bool matches(Solver&, SetElement, InvariantSet key, InvariantSet watch);
        void explainMatch(Solver&, SetElement, InvariantSet key, InvariantSet watch, ClauseBuilder& clause);
        void onKeyMatch(Solver&, SetElement, InvariantSet key, InvariantSet watch);
    };

    // Note: The keys of these maps could be obtained from the stored values
    using LocationSets = std::unordered_map<MemoryLocation, InvariantSet, MemoryLocationHash>;

    ExactSetIndex exactSetIndex;

    TheoryData<LocationSetInfo, TheoryId::InclusiveLocationInvariantSets> inclusiveInfos;
    TheoryData<LocationSetInfo, TheoryId::ExclusiveLocationInvariantSets> exclusiveInfos;
    TheoryData<LocationSetInfo, TheoryId::PathInvariantSets> pathInfos;
    TheoryData<ExactSetInfo, TheoryId::ExactInvariantSets> exactInfos;

    LocationSets inclusiveSets;
    LocationSets exclusiveSets;
    LocationSets pathSets;
    std::unordered_map<ExactSetKey, InvariantSet, ExactSetHash> exactSets;
};

}
