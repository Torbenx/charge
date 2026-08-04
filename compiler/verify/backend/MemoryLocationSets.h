#pragma once

#include <verify/backend/KeyWatches.h>
#include <verify/backend/PrefixIndex.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/UninterpretedEquality.h>
#include <verify/backend/Use.h>

#include <unordered_map>

namespace verify::backend {

struct SharedElementSets {
    Set setA;
    Set setB;
};

struct SharedElementReason : private PackedReason<SharedElementSets, uint32_t> {
    SharedElementReason(Sets::ElementId element, Set setA, Set setB)
        : PackedReason({ setA, setB }, element.id()) { }

    SharedElementSets sets() const { return data(); }
    Sets::ElementId element() const { return Sets::ElementId(tag()); }
};

struct MemoryLocationSetsParams {
    Sort setSort;
    TypedReasonKind<SharedElementReason> declarationsShareElementReason;
    PrefixIndex::Params prefixParams;
    KeyWatchesParams watchesParams;
};

template<typename Derived>
struct MemoryLocationSets {
    using Params = MemoryLocationSetsParams;
    using ElementId = Sets::ElementId;
    using Containment = Sets::Containment;

    MemoryLocationSets(Solver&);

    void propagateContainment(Solver&, ElementId, Sets::Containment);
    void propagateRewrite(Solver&, Use);

    bool testReason(Solver&, Bool, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);

    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

    Sets& baseTheory(Solver&);

    void checkInvariances(Solver&);

private:
    //! Registers the words of the containments once they name the declaration of the representative
    /*!
    The key of an element is the set of its representative and its watches are the sets of all its
    containments, both compared by the declarations of their locations.
    */
    struct PendingWatches : KeyWatches<PendingWatches, Containment, Containment> {
        static constexpr KeyWatchesParams PARAMS = Derived::PARAMS.watchesParams;

        MemoryLocationSets& locationSets();

        bool matches(Solver&, ElementId, Containment key, Containment watch);
        void addValueUses(Solver&, ElementId, Containment watch, Use);
        void onKeyMatch(Solver&, ElementId, Containment key, Containment watch);
    };

    static auto setHandle(Set set) { return typename Derived::SetHandle(set); }

    MemoryLocation locationOf(Set set) {
        return derived().locationOf(setHandle(set));
    }
    Bool containmentOf(Solver&, PrefixIndex::WordId);
    MemoryDeclaration declarationOf(PrefixIndex::WordId word) {
        return locationOf(prefixes.containmentOf(word).set()).declaration;
    }

    static constexpr MemoryLocationSetsParams params();
    Derived& derived() { return static_cast<Derived&>(*this); }

    void addWord(Solver&, ElementId, Containment);

    PendingWatches pendingWatches;

    PrefixIndex prefixes;
};

}
