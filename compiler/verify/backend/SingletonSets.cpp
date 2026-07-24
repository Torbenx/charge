#include <verify/backend/SingletonSets.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

namespace {
    struct AggPair {
        Value setA;
        Value setB;
    };
}

struct InSingletonReason : private PackedReason<AggPair, uint32_t> {
    InSingletonReason(Sets::ElementId element, Value setA, Value setB)
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

Value SingletonSets::singleton(Solver& solver, Value element) {
    VERIFY(sortOf(element.theory()) == params.elementSort);
    auto& info = elementInfos[element];
    if (!info.singletonSet.has_value()) {
        Value set = solver.impl().newValue(params.singletonTheory);
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
        int_t decisionLevel = solver.currentDecisionLevel();
        VERIFY(decisionLevel >= 0); // No positive set assignments are made without a decision
        state.singleton = cont.set();
        state.decisionLevel = (uint32_t)decisionLevel;
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

void SingletonSets::newDecisionLevel(Solver&) { }

void SingletonSets::beginBacktrack(Solver& solver) {
    int_t decisionLevel = solver.currentDecisionLevel();
    for (auto& state : elementStates) {
        if ((int_t)state.decisionLevel > decisionLevel)
            state.singleton.reset();
    }
}

void SingletonSets::endBacktrack(Solver&) { }

Sets& SingletonSets::sets(Solver& solver) { return solver.impl().setTheory(params.setSort); }

}