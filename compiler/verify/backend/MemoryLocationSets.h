#pragma once

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

    TheoryData<SetInfo, TheoryId::MemoryLocationSets> setInfos;
    //! The locations are repeated as the keys here, but that keeps the lookup simple
    std::unordered_map<MemoryLocation, Value, LocationHash> sets;
};

}
