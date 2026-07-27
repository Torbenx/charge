#include <verify/backend/MemoryLocationSets.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

MemoryLocationSets::MemoryLocationSets(Solver& solver)
    : setInfos(solver) { }

Value MemoryLocationSets::set(Solver& solver, MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    Value newSet = solver.impl().newValue(TheoryId::MemoryLocationSets);
    setInfos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

}
