#include <verify/backend/InvariantSets.h>

#include <verify/backend/MemoryLocationSets.impl.h>
#include <verify/backend/SolverImpl.h>

#include <algorithm>

namespace verify::backend {

template struct MemoryLocationSets<InvariantSets>;

struct SingletonToInclusiveReason {
    Sets::ElementId element;
    InvariantSet singletonSet;
};

InvariantSets::InvariantSets(Solver& solver)
    : Base(solver), inclusiveInfos(solver), exclusiveInfos(solver), pathInfos(solver), singletonInfos(solver) { }

template<typename Info, TheoryId theory>
static InvariantSet locationSet(
    Solver& solver,
    std::unordered_map<MemoryLocation, InvariantSet, MemoryLocationHash>& sets,
    TheoryData<Info, theory>& infos,
    MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.impl().newValue(theory);
    infos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

InvariantSet InvariantSets::inclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, inclusiveSets, inclusiveInfos, location);
}

InvariantSet InvariantSets::exclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, exclusiveSets, exclusiveInfos, location);
}

InvariantSet InvariantSets::pathSet(Solver& solver, MemoryLocation location) {
    int_t oldCount = pathSets.size();
    InvariantSet set = locationSet(solver, pathSets, pathInfos, location);

    // No location is strictly above the whole declaration, so its path set is the empty one. Without
    // this the emptiness is only found once the element is known to be somewhere in the declaration.
    // TODO: There may be problem with elements that are later rewritten to the identity which are
    //       not caught here.
    if ((int_t)pathSets.size() != oldCount && location.member == identity_member)
        solver.addClause({ baseTheory(solver).isEmpty(solver, set) });

    return set;
}

InvariantSet InvariantSets::singletonSet(Solver& solver, MemoryLocation location, Invariant invariant) {
    SingletonKey key { location, invariant };
    auto it = singletonSets.find(key);
    if (it != singletonSets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.impl().newValue(TheoryId::InvariantSingletonSets);
    singletonInfos[newSet].location = location;
    singletonInfos[newSet].invariant = invariant;
    singletonSets.emplace(key, newSet);
    return newSet;
}

void InvariantSets::addWords(Solver& solver, PrefixIndex& prefixes, ElementId element, Containment cont) {
    PrefixIndex::Role role;
    PrefixIndex::SelfInclusion inclusion;
    switch (cont.set().theory()) {
    case TheoryId::InclusiveLocationInvariantSets:
        role = cont.contained() ? PrefixIndex::Role::Path : PrefixIndex::Role::Prefix;
        inclusion = PrefixIndex::SelfInclusion::Inclusive;
        break;
    case TheoryId::ExclusiveLocationInvariantSets:
        role = cont.contained() ? PrefixIndex::Role::Path : PrefixIndex::Role::Prefix;
        inclusion = PrefixIndex::SelfInclusion::Exclusive;
        break;
    case TheoryId::PathInvariantSets:
        role = cont.contained() ? PrefixIndex::Role::Prefix : PrefixIndex::Role::Path;
        inclusion = PrefixIndex::SelfInclusion::Inclusive;
        break;
    case TheoryId::InvariantSingletonSets:
        if (!cont.contained())
            return;
        role = PrefixIndex::Role::Prefix;
        inclusion = PrefixIndex::SelfInclusion::Exclusive;
        break;
    default:
        VERIFY_NOT_REACHED();
    }
    Member member = locationOf((InvariantSet)cont.set()).member;
    prefixes.addWord(solver, member, element, cont, role, inclusion);
}

void InvariantSets::propagateContainment(Solver& solver, ElementId element, Containment containment) {
    InvariantSet set = (InvariantSet)containment.set();
    VERIFY(isInvariantSet(set));

    if (containment.contained() && set.theory() == TheoryId::InvariantSingletonSets) {
        // in singletonSet(loc, I) => in inclusiveSet(loc)
        baseTheory(solver).assignTrue(solver, element, Sets::in(inclusiveSet(solver, locationOf(set))),
            makeReason<ReasonKind::InvariantSingletonToInclusive>({ element, (InvariantSet)containment.set() }));

        // in singleton1 and in singleton2 => singleton1 = singleton2
        auto& state = stateOf(element);
        if (!state.singleton.has_value()) {
            state.singleton = set;
            singletonTrace.push(element);
        } else {
            InvariantSet singleton = state.singleton.value();
            solver.assignTrue(solver.equality(singleton, set),
                makeReason<ReasonKind::InvariantSingletonSetsShareElement>({ element, singleton, set }));
        }

        // TODO: Detect in singletonSet(loc1, I) and not in singletonSet(loc2, I) as conflict when
        //       loc1 and loc2 are same in the current rewrite.
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
    } else if (reason.kind() == ReasonKind::InvariantSingletonToInclusive) {
        auto [element, singleton] = reason.get<ReasonKind::InvariantSingletonToInclusive>();
        return baseTheory(solver).assignedTrue(solver, element, Sets::in(singleton));
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
    } else if (reason.kind() == ReasonKind::InvariantSingletonToInclusive) {
        auto [element, singleton] = reason.get<ReasonKind::InvariantSingletonToInclusive>();
        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, element, !Sets::in(singleton)));
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
    Base::checkInvariances(solver);
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

}
