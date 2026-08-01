#pragma once

#include <verify/backend/PrefixIndex.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>
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
    UseKind pendingRewriteUse;
    UseKind representativeRewriteUse;
};

template<typename Derived, typename PrefixImpl>
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
    //! A containment whose declaration is not (yet) equal to the representative
    struct PendingContainment {
        ElementId element;
        Containment containment;
        //! Becomes true when equality to the represnetative is detected
        bool promoted = false;
    };

    struct ElementState {
        //! The set of the first location found to contain the element
        std::optional<Set> representative;
        //! The pending containments of this element, in increasing order
        std::vector<TracePosition> pendingPositions;
    };

    static auto setHandle(Set set) { return typename Derived::SetHandle(set); }

    MemoryLocation locationOf(Set set) {
        return derived().locationOf(setHandle(set));
    }
    Bool containmentOf(Solver&, PrefixIndexWordId);
    MemoryDeclaration declarationOf(PrefixIndexWordId word) {
        return locationOf(prefixes.containmentOf(word).set()).declaration;
    }

    static constexpr MemoryLocationSetsParams params();
    Derived& derived() { return static_cast<Derived&>(*this); }

    ElementState& stateOf(ElementId element) {
        if (element.id() >= elementStates.size())
            elementStates.resize(element.id() + 1);
        return elementStates[element.id()];
    }

    //! Whether \p declaration is equal to the representative declaration of the element
    bool joinedRepresentative(Solver&, const ElementState&, MemoryDeclaration declaration);

    void addWord(Solver&, ElementId, Containment);
    void addPending(Solver&, ElementId, Containment);
    void promotePending(Solver&, const ElementState&, TracePosition);
    void promotePendingOf(Solver&, ElementId);

    std::vector<ElementState> elementStates;
    //! The pending containments, referenced by \ref ElementState::pendingPositions
    Trace<PendingContainment> pending;
    Trace<ElementId> representativeTrace;
    Trace<TracePosition> promotionTrace;

    PrefixIndex<PrefixImpl> prefixes;
};

}
