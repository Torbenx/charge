#include <verify/backend/InvariantSets.h>

#include <verify/backend/MemoryLocationSets.impl.h>
#include <verify/backend/SolverImpl.h>

#include <algorithm>

namespace verify::backend {

template struct MemoryLocationSets<InvariantSets, InvariantPrefixes>;

InvariantSets::InvariantSets(Solver& solver)
    : Base(solver), setInfos(solver) { }

InvariantSet InvariantSets::locationSet(Solver& solver, LocationSets& sets, TheoryId theory, MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.impl().newValue(theory);
    setInfos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

InvariantSet InvariantSets::inclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, inclusiveSets, TheoryId::InclusiveLocationInvariantSets, location);
}

InvariantSet InvariantSets::exclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, exclusiveSets, TheoryId::ExclusiveLocationInvariantSets, location);
}

InvariantSet InvariantSets::leafSet(Solver& solver, MemoryLocation location, Invariant invariant) {
    LeafKey key { location, invariant };
    auto it = leafSets.find(key);
    if (it != leafSets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.impl().newValue(TheoryId::LeafInvariantSets);
    setInfos[newSet].location = location;
    setInfos[newSet].invariant = invariant;
    leafSets.emplace(key, newSet);
    return newSet;
}

InvariantWord InvariantSets::toWord(InvariantSet set) const {
    Member member = locationOf(set).member;
    switch (set.theory()) {
    case TheoryId::InclusiveLocationInvariantSets:
        return InvariantWord::inclusive(member);
    case TheoryId::ExclusiveLocationInvariantSets:
        return InvariantWord::exclusive(member);
    case TheoryId::LeafInvariantSets:
        return InvariantWord::leaf(member, invariantOf(set));
    default:
        VERIFY_NOT_REACHED();
    }
}

void InvariantSets::propagateContainment(Solver& solver, ElementId element, Containment containment) {
    InvariantSet set = (InvariantSet)containment.set();
    VERIFY(isInvariantSet(set));

    if (containment.contained() && set.theory() == TheoryId::LeafInvariantSets) {
        auto& state = stateOf(element);
        if (!state.leaf.has_value()) {
            state.leaf = set;
            leafTrace.push(element);
        } else {
            InvariantSet leaf = state.leaf.value();
            solver.assignTrue(solver.equality(leaf, set),
                makeReason<ReasonKind::InvariantLeafSetsShareElement>({ element, leaf, set }));
        }
    }

    Base::propagateContainment(solver, element, containment);
}

bool InvariantSets::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    // Both conclusions are drawn from the same two containments, only what they say differs
    if (reason.kind() == ReasonKind::InvariantLeafSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setA))
            && baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setB));
    }

    return Base::testReason(solver, assignedLiteral, reason);
}

ClauseAndIndex InvariantSets::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::InvariantLeafSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();

        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setA)));
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setB)));
        return { solver.viewClause(clause), 0 };
    }

    return Base::reasonToClause(solver, assignedLiteral, reason);
}

void InvariantSets::newDecisionLevel(Solver& solver) {
    Base::newDecisionLevel(solver);
    leafTrace.newDecisionLevel(solver);
}

void InvariantSets::beginBacktrack(Solver& solver) {
    Base::beginBacktrack(solver);
    for (ElementId element : leafTrace.backtrackedReverse(solver))
        stateOf(element).leaf.reset();
    leafTrace.truncate(solver);
}

void InvariantSets::checkInvariances(Solver& solver) {
    leafTrace.checkInvariances(solver);

    // An element has a leaf exactly when it is on the trace, and it is on it only once.
    std::vector<bool> onLeafTrace;
    onLeafTrace.resize(elementStates.size());
    auto markOnTrace = [](std::vector<bool>& marks, ElementId element) {
        VERIFY(element.id() < marks.size());
        VERIFY(!marks[element.id()]);
        marks[element.id()] = true;
    };
    for (ElementId element : leafTrace)
        markOnTrace(onLeafTrace, element);

    for (int_t i = 0; i < (int_t)elementStates.size(); i++) {
        VERIFY(elementStates[i].leaf.has_value() == onLeafTrace[i]);
    }
}

void InvariantPrefixes::appendLetters(Solver& solver, InvariantWord word, std::vector<InvariantLetter>& out) {
    memberBuffer.clear();
    solver.impl().members.appendRewrite(word.member, memberBuffer);

    for (Member letter : memberBuffer) {
        // Stepping into a member steps below the location first, which puts the exclusive set of a
        // location above the sets of all of its members
        out.push_back(InvariantLetter::narrow());
        out.push_back(InvariantLetter::member(letter));
    }

    if (!word.suffix.isEmpty())
        out.push_back(word.suffix.toLetter());
}

void InvariantPrefixes::explainLetters(Solver& solver, InvariantWord word, ClauseBuilder& clause) {
    // Only the member of the location is rewritten
    solver.impl().members.explainRewrite(solver, word.member, clause);
}

}
