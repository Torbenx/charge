#include <verify/backend/SingletonSets.h>

#include <verify/backend/Sets.h>

namespace verify::backend {

namespace {
    struct AggPair {
        Set setA;
        Set setB;
    };
}

struct InSingletonReason : private PackedReason<AggPair, uint32_t> {
    InSingletonReason(Sets::ElementId element, Set setA, Set setB)
        : PackedReason({ setA, setB }, element.id()) { }
    AggPair sets() const { return data(); }
    Sets::ElementId element() const { return Sets::ElementId(tag()); }
};

SingletonSets::SingletonSets(Solver& solver, const SingletonSetsParams& params)
    : params(params)
    , elementInfos(solver, params.elementSort)
    , singletonInfos(solver, params.singletonTheory) {
    VERIFY(sortOf(params.singletonTheory) == params.setSort);
}

Set SingletonSets::singleton(Solver& solver, Value element) {
    VERIFY(sortOf(element.theory()) == params.elementSort);
    auto& info = elementInfos[element];
    if (!info.singletonSet.has_value()) {
        Set set = (Set)solver.newValue(params.singletonTheory);
        info.singletonSet = set;
        singletonInfos[set].element = element;
    }
    return info.singletonSet.value();
}

void SingletonSets::propagateContainment(Solver& solver, Sets::ElementId element, Sets::Containment cont) {
    if (!cont.contained())
        return;
    auto& state = stateOf(element);
    if (!state.singleton.has_value()) {
        state.singleton = cont.set();
        singletonTrace.push(element);
    } else {
        solver.assignTrue(solver.equality(state.singleton.value(), cont.set()),
            makeReason(params.inSingletonReason, { element, state.singleton.value(), cont.set() }));
    }
}

bool SingletonSets::testReason(Solver& solver, Bool, const Reason& reason) {
    Sets& sets = this->sets(solver);
    auto data = reason.get(params.inSingletonReason);
    auto [setA, setB] = data.sets();
    return sets.assignedTrue(solver, data.element(), Sets::in(setA))
        && sets.assignedTrue(solver, data.element(), Sets::in(setB));
}

ClauseAndIndex SingletonSets::reasonToClause(Solver& solver, Bool equality, const Reason& reason) {
    Sets& sets = this->sets(solver);
    auto data = reason.get(params.inSingletonReason);
    auto [setA, setB] = data.sets();
    auto clause = solver.beginClause();
    clause.add(solver, equality);
    clause.add(solver, sets.mapToBool(solver, data.element(), !Sets::in(setA)));
    clause.add(solver, sets.mapToBool(solver, data.element(), !Sets::in(setB)));
    return { solver.viewClause(clause), 0 };
}

void SingletonSets::newDecisionLevel(Solver& solver) {
    singletonTrace.newDecisionLevel(solver);
}

void SingletonSets::beginBacktrack(Solver& solver) {
    for (Sets::ElementId element : singletonTrace.backtrackedReverse(solver))
        stateOf(element).singleton.reset();
    singletonTrace.truncate(solver);
}

void SingletonSets::endBacktrack(Solver&) { }

void SingletonSets::checkInvariances(Solver& solver) {
    singletonTrace.checkInvariances(solver);

    // An element has a singleton exactly when it is on the trace, and it is on it only once
    std::vector<bool> onTrace;
    onTrace.resize(elementStates.size());
    for (Sets::ElementId element : singletonTrace) {
        VERIFY(element.id() < onTrace.size());
        VERIFY(!onTrace[element.id()]);
        onTrace[element.id()] = true;
    }
    for (int_t i = 0; i < (int_t)elementStates.size(); i++)
        VERIFY(elementStates[i].singleton.has_value() == onTrace[i]);
}

Sets& SingletonSets::sets(Solver& solver) { return solver.setTheory(params.setSort); }

}