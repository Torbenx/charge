#pragma once

#include <verify/backend/MemberPrefixes.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>
#include <verify/backend/UninterpretedEquality.h>
#include <verify/backend/Use.h>

#include <unordered_map>

namespace verify::backend {

//! The location sets described by a (declaration, member) pair
/*!
A value of this theory is the set of all locations of scalar type acessible from a memory location.
Note that these sets are may not contain any elements at all.
*/
struct MemoryLocationSets {
    using ElementId = Sets::ElementId;
    using Containment = Sets::Containment;

    MemoryLocationSets(Solver&);

    Value set(Solver&, MemoryLocation location);
    Value set(Solver& solver, MemoryDeclaration declaration, Member member) {
        return set(solver, { declaration, member });
    }

    MemoryLocation locationOf(Value set) const {
        VERIFY(set.theory() == TheoryId::MemoryLocationSets);
        return setInfos[set].location;
    }

    void propagateContainment(Solver&, ElementId, Sets::Containment);
    void propagateRewrite(Solver&, Use);

    bool testReason(Solver&, Bool, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);

    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);
    void endBacktrack(Solver& solver) { prefixes.endBacktrack(solver); }

    void checkInvariances(Solver&);

private:
    struct SetInfo {
        SetInfo() = default;
        MemoryLocation location { MemoryDeclaration(INVALID_VALUE) };
    };

    //! A containment whose declaration is not (yet) equal to the representative
    struct PendingContainment {
        ElementId element;
        Containment containment;
        //! Becomes true when equality to the represnetative is detected
        bool promoted = false;
    };

    struct ElementState {
        //! The set of the first location found to contain the element
        std::optional<Value> representative;
        //! The pending containments of this element, in increasing order
        std::vector<TracePosition> pendingPositions;
    };

    struct LocationHash {
        size_t operator()(MemoryLocation location) const {
            size_t hash = 0;
            hash_combine(hash, std::bit_cast<uint32_t>((Value)location.declaration));
            hash_combine(hash, std::bit_cast<uint32_t>((Value)location.member));
            return hash;
        }
    };

    Bool containmentOf(Solver&, MemberPrefixes::WordId);
    MemoryDeclaration declarationOf(MemberPrefixes::WordId word) {
        return locationOf(prefixes.containmentOf(word).set()).declaration;
    }

    Sets& memorySets(Solver&);

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

    TheoryData<SetInfo, TheoryId::MemoryLocationSets> setInfos;
    // Note: The location key could be obtained from the stored value
    std::unordered_map<MemoryLocation, Value, LocationHash> sets;

    std::vector<ElementState> elementStates;
    //! The pending containments, referenced by \ref ElementState::pendingPositions
    Trace<PendingContainment> pending;
    Trace<ElementId> representativeTrace;
    Trace<TracePosition> promotionTrace;

    MemberPrefixes prefixes;
};

}
