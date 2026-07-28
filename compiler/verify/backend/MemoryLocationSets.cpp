#include <verify/backend/MemoryLocationSets.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

MemoryLocationSets::MemoryLocationSets(Solver& solver)
    : setInfos(solver)
    , prefixes(solver) { }

Value MemoryLocationSets::set(Solver& solver, MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    Value newSet = solver.impl().newValue(TheoryId::MemoryLocationSets);
    setInfos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

Bool MemoryLocationSets::containmentOf(Solver& solver, MemberPrefixes::WordId word) {
    Value set = prefixes.payloadOf(word);
    Sets::ElementId element = prefixes.elementOf(word);
    // A path is a set the element is contained in, a prefix candidate is one it is not
    Sets::Containment containment(set, prefixes.isPath(word));
    return solver.impl().memorySets.mapToBool(solver, element, containment);
}

void MemoryLocationSets::propagateContainment(Solver& solver, Sets::ElementId element, Sets::Containment containment) {
    Value set = containment.set();
    VERIFY(set.theory() == TheoryId::MemoryLocationSets);
    MemoryLocation location = locationOf(set);

    // TODO: MemoryDeclarations are ignored.

    // The containment is propagated exactly once per decision level it is assigned at, so the word
    // registered here is unregistered again by the same backtrack that reverts the assignment.
    if (containment.contained())
        prefixes.addPath(solver, element, location.member, set);
    else
        prefixes.addPrefixCandidate(solver, element, location.member, set);
}

bool MemoryLocationSets::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get<ReasonKind::MemberPrefixHit>();
    return prefixes.isPrefixOf(data.prefix, data.path)
        && solver.assignedTrue(containmentOf(solver, data.prefix))
        && solver.assignedTrue(containmentOf(solver, data.path));
}

ClauseAndIndex MemoryLocationSets::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get<ReasonKind::MemberPrefixHit>();

    ClauseBuilder clause = solver.beginClause();
    clause.add(solver, assignedLiteral);
    // The member of the prefix is a prefix of the member of the path, which makes the set of the
    // path a subset of the set of the prefix. So the element cannot be excluded from the one and
    // contained in the other at the same time.
    clause.add(solver, !containmentOf(solver, data.prefix));
    clause.add(solver, !containmentOf(solver, data.path));
    prefixes.explainPrefix(solver, data.prefix, data.path, clause);
    return { solver.viewClause(clause), 0 };
}

}
