#pragma once

#include <verify/backend/Data.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>

#include <unordered_map>

namespace verify::backend {

//! The sets of invariants described by a memory location
/*!
A leaf of this theory is the instance of one invariant at one memory location.
There are three kinds of sets:
- An inclusive location set holds the invariants of its location and those of its members
- An exclusive location set holds only the invariants of its members
- An invariant leaf set holds a single invariant

Note that these sets may not contain any elements at all, even the leaf sets.
*/
struct InvariantSets {
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

    // Note: The keys of these maps could be obtained from the stored values
    using LocationSets = std::unordered_map<MemoryLocation, Value, MemoryLocationHash>;

    Value locationSet(Solver&, LocationSets&, TheoryId, MemoryLocation);

    SortData<SetInfo, Sort::InvariantSet> setInfos;

    LocationSets inclusiveSets;
    LocationSets exclusiveSets;
    std::unordered_map<LeafKey, Value, LeafHash> leafSets;
};

}
