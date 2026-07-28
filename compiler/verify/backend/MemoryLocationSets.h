#pragma once

#include <verify/backend/MemberPrefixes.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>

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
*/
struct MemoryLocationSets {
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

    void propagateContainment(Solver&, Sets::ElementId, Sets::Containment);

    //! Bring the prefix index up to date with the rewrites applied by the Members theory
    void propagateRewrites(Solver& solver) { prefixes.propagateRewrites(solver); }

    bool testReason(Solver&, Bool, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);

    void newDecisionLevel(Solver& solver) { prefixes.newDecisionLevel(solver); }
    void beginBacktrack(Solver& solver) { prefixes.beginBacktrack(solver); }
    void endBacktrack(Solver& solver) { prefixes.endBacktrack(solver); }

    void checkInvariances(Solver& solver) { prefixes.checkInvariances(solver); }

private:
    struct SetInfo {
        SetInfo() = default;
        MemoryLocation location { MemoryDeclaration(INVALID_VALUE) };
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

    TheoryData<SetInfo, TheoryId::MemoryLocationSets> setInfos;
    //! The locations are repeated as the keys here, but that keeps the lookup simple
    std::unordered_map<MemoryLocation, Value, LocationHash> sets;

    //! Detects the contradicting containments
    MemberPrefixes prefixes;
};

}
