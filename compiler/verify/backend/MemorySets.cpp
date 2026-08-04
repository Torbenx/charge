#include <verify/backend/MemorySets.h>

#include <verify/backend/MemoryLocationSets.impl.h>

namespace verify::backend {

template struct MemoryLocationSets<MemorySets>;

MemorySets::MemorySets(Solver& solver)
    : Base(solver), setInfos(solver) { }

MemorySet MemorySets::set(Solver& solver, MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    MemorySet newSet = (MemorySet)solver.impl().newValue(TheoryId::MemoryLocationSets);
    setInfos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

}