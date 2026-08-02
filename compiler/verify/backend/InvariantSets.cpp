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

InvariantSet InvariantSets::singletonSet(Solver& solver, MemoryLocation location, Invariant invariant) {
    SingletonKey key { location, invariant };
    auto it = singletonSets.find(key);
    if (it != singletonSets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.impl().newValue(TheoryId::InvariantSingletonSets);
    setInfos[newSet].location = location;
    setInfos[newSet].invariant = invariant;
    singletonSets.emplace(key, newSet);
    return newSet;
}

InvariantWord InvariantSets::toWord(InvariantSet set) const {
    Member member = locationOf(set).member;
    switch (set.theory()) {
    case TheoryId::InclusiveLocationInvariantSets:
        return InvariantWord::inclusive(member);
    case TheoryId::ExclusiveLocationInvariantSets:
        return InvariantWord::exclusive(member);
    case TheoryId::InvariantSingletonSets:
        return InvariantWord::singleton(member, invariantOf(set));
    default:
        VERIFY_NOT_REACHED();
    }
}

void InvariantSets::addWords(Solver& solver, Prefixes& prefixes, ElementId element, Containment cont) {
    // If an element is in a location set but not in the location set of some prefix of it, thats a conflict.
    auto role = cont.contained() ? PrefixRole::Path : PrefixRole::Candidate;
    prefixes.addWord(solver, toWord((InvariantSet)cont.set()), element, cont, role);
}

void InvariantSets::propagateContainment(Solver& solver, ElementId element, Containment containment) {
    InvariantSet set = (InvariantSet)containment.set();
    VERIFY(isInvariantSet(set));

    if (containment.contained() && set.theory() == TheoryId::InvariantSingletonSets) {
        auto& state = stateOf(element);
        if (!state.singleton.has_value()) {
            state.singleton = set;
            singletonTrace.push(element);
        } else {
            InvariantSet singleton = state.singleton.value();
            solver.assignTrue(solver.equality(singleton, set),
                makeReason<ReasonKind::InvariantSingletonSetsShareElement>({ element, singleton, set }));
        }
    }

    Base::propagateContainment(solver, element, containment);
}

bool InvariantSets::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    // Both conclusions are drawn from the same two containments, only what they say differs
    if (reason.kind() == ReasonKind::InvariantSingletonSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setA))
            && baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setB));
    }

    return Base::testReason(solver, assignedLiteral, reason);
}

ClauseAndIndex InvariantSets::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::InvariantSingletonSetsShareElement) {
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
    singletonTrace.newDecisionLevel(solver);
}

void InvariantSets::beginBacktrack(Solver& solver) {
    Base::beginBacktrack(solver);
    for (ElementId element : singletonTrace.backtrackedReverse(solver))
        stateOf(element).singleton.reset();
    singletonTrace.truncate(solver);
}

void InvariantSets::checkInvariances(Solver& solver) {
    singletonTrace.checkInvariances(solver);

    // An element has a singleton exactly when it is on the trace, and it is on it only once
    std::vector<bool> onTrace;
    onTrace.resize(elementStates.size());
    auto markOnTrace = [](std::vector<bool>& marks, ElementId element) {
        VERIFY(element.id() < marks.size());
        VERIFY(!marks[element.id()]);
        marks[element.id()] = true;
    };
    for (ElementId element : singletonTrace)
        markOnTrace(onTrace, element);

    for (int_t i = 0; i < (int_t)elementStates.size(); i++) {
        VERIFY(elementStates[i].singleton.has_value() == onTrace[i]);
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
