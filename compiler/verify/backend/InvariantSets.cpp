#include <verify/backend/InvariantSets.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

InvariantSets::InvariantSets(Solver& solver)
    : setInfos(solver) { }

Value InvariantSets::locationSet(Solver& solver, LocationSets& sets, TheoryId theory, MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    Value newSet = solver.impl().newValue(theory);
    setInfos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

Value InvariantSets::inclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, inclusiveSets, TheoryId::InclusiveLocationInvariantSets, location);
}

Value InvariantSets::exclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, exclusiveSets, TheoryId::ExclusiveLocationInvariantSets, location);
}

Value InvariantSets::leafSet(Solver& solver, MemoryLocation location, Invariant invariant) {
    LeafKey key { location, invariant };
    auto it = leafSets.find(key);
    if (it != leafSets.end())
        return it->second;

    Value newSet = solver.impl().newValue(TheoryId::LeafInvariantSets);
    setInfos[newSet].location = location;
    setInfos[newSet].invariant = invariant;
    leafSets.emplace(key, newSet);
    return newSet;
}

}
