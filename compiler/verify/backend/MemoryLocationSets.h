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
A value of this theory is the set of all locations of scalar type a memory location is composed
of. A location of scalar type is composed of itself alone and any other location is composed of
its members, so such a set describes its location exactly. The pair is therefore not mapped to
the set, it is just a different representation of it.

Note that unlike a singleton set such a set is neither guaranteed to contain exactly one element
nor to be non-empty.

\section containment Contradicting containments

Extending the member of a location narrows the location, so for locations of the same declaration
the set of a member is a subset of the set of every prefix of that member. An element contained in
the set of a location must therefore be contained in the sets of all locations above it, and an
element not contained in one of those cannot be contained in the location itself.

This is detected by MemberPrefixes: the members of the sets containing the element form the set A
and the members of the sets not containing it form the set B, so a prefix relation between the two
sets is exactly the contradiction described above.

\section declarations Declarations

All of the above only holds for locations of the same declaration, because distinct declarations
describe distinct memory. An element of the sets of two locations is therefore taken to mean that
their declarations are equal, which is propagated eagerly against the declaration of the first
containing location, the representative of the element.

A containment is only handed to MemberPrefixes once its declaration is known to be the one of the
representative, so that every word of an element belongs to one declaration and a prefix relation
between two of them is a contradiction. Until then it is a pending containment, waiting for a use to
report that the declarations were joined, see UninterpretedEquality::addUse().

Note that the equality against the representative is only assigned when a containment is propagated,
so it is not applied yet at that point. Deferring the containment until it is means that a word is
never registered while it is incomparable to the ones already there, which would make a prefix
relation between them a conflict that cannot be justified.
*/
struct MemoryLocationSets {
    using ElementId = Sets::ElementId;
    using Containment = Sets::Containment;

    MemoryLocationSets(Solver&);

    //! The set describing \p location
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

    //! A containment whose declaration is not known to be the one of the representative yet
    struct PendingContainment {
        ElementId element;
        Containment containment;
        bool promoted = false; //!< Whether the word was registered by now
    };

    struct ElementState {
        //! The set of the first location found to contain the element
        /*!
        The declarations of all other containing locations are equated to the one of this location.
        */
        std::optional<Value> representative;
        //! The pending containments of this element, in increasing order
        std::vector<uint32_t> pendingIndices;
    };

    struct LocationHash {
        size_t operator()(MemoryLocation location) const {
            size_t hash = 0;
            hash_combine(hash, std::bit_cast<uint32_t>((Value)location.declaration));
            hash_combine(hash, std::bit_cast<uint32_t>((Value)location.member));
            return hash;
        }
    };

    //! The containment literal a word of \ref prefixes was registered for
    Bool containmentOf(Solver&, MemberPrefixes::WordId);
    //! The declaration of the location a word of \ref prefixes was registered for
    MemoryDeclaration declarationOf(MemberPrefixes::WordId word) {
        return locationOf(prefixes.containmentOf(word).set()).declaration;
    }

    Sets& memorySets(Solver&);

    ElementState& stateOf(ElementId element) {
        if (element.id() >= elementStates.size())
            elementStates.resize(element.id() + 1);
        return elementStates[element.id()];
    }

    //! Whether \p declaration is known to be the declaration of the representative of the element
    bool joinedRepresentative(Solver&, const ElementState&, MemoryDeclaration declaration);

    void addWord(Solver&, ElementId, Containment);
    void addPending(Solver&, ElementId, Containment);
    void promotePending(Solver&, const ElementState&, uint32_t index);
    void promotePendingOf(Solver&, ElementId);

    TheoryData<SetInfo, TheoryId::MemoryLocationSets> setInfos;
    //! The locations are repeated as the keys here, but that keeps the lookup simple
    std::unordered_map<MemoryLocation, Value, LocationHash> sets;

    std::vector<ElementState> elementStates;
    //! The pending containments, indexed by the position they are named by in \ref ElementState::pendingIndices
    Trace<PendingContainment, uint32_t> pending;
    //! The elements whose representative was recorded, in the order they were recorded
    Trace<ElementId> representativeTrace;
    //! The pending containments that were promoted, in the order they were promoted
    Trace<uint32_t> promotionTrace;

    //! Detects the contradicting containments
    MemberPrefixes prefixes;
};

}
