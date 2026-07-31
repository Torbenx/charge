#include <verify/backend/MemorySets.h>

#include <verify/backend/MemoryLocationSets.impl.h>

namespace verify::backend {

template struct MemoryLocationSets<MemorySets, MemberPrefixes>;

MemorySets::MemorySets(Solver& solver)
    : Base(solver), setInfos(solver) { }

Value MemorySets::set(Solver& solver, MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    Value newSet = solver.impl().newValue(TheoryId::MemoryLocationSets);
    setInfos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

void MemberPrefixes::appendLetters(Solver& solver, Member expression, std::vector<Member>& out) {
    solver.impl().members.appendRewrite(expression, out);
}

void MemberPrefixes::explainLetters(Solver& solver, Member expression, ClauseBuilder& clause) {
    solver.impl().members.explainRewrite(solver, expression, clause);
}

}