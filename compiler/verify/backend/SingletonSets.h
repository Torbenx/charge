#pragma once

#include <verify/backend/Solver.h>
#include <verify/backend/Sets.h>

namespace verify::backend {

struct SingletonSetsParams {
    ValueKind elementKind;
    ValueKind setKind;
    TheoryId singletonTheory;
    TypedReasonKind<InSingletonReason> inSingletonReason;
};

struct SingletonSets {
    SingletonSets(Solver&, const SingletonSetsParams&);

    Value singleton(Solver&, Value);
    Value element(Value singletonSet) {
        VERIFY(singletonSet.theory() == params.singletonTheory);
        return singletonInfos[singletonSet].element;
    }

    void propagateContainment(Solver&, Sets::ElementId, Sets::Containment);

    bool testReason(Solver&, BooleanValue, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&);

    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

private:
    struct ElementInfo {
        ElementInfo() = default;
        std::optional<Value> singletonSet;
    };
    struct SingletonInfo {
        SingletonInfo() = default;
        Value element = INVALID_VALUE;
    };
    struct ElementState {
        std::optional<Value> singleton;
        uint32_t decisionLevel = limits::max; // only meaningful when 'singleton' has a value
    };

    Sets& sets(Solver& solver);

    ElementState& stateOf(Sets::ElementId elem) {
        if (elem.id() >= elementStates.size()) {
            elementStates.resize(elem.id() + 1);
        }
        return elementStates[elem.id()];
    }

    SingletonSetsParams params;

    std::vector<ElementState> elementStates;

    KindData<ElementInfo> elementInfos;
    TheoryData<SingletonInfo> singletonInfos;
};

}