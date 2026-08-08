#pragma once

#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>

namespace verify::backend {

struct SingletonSetsParams {
    Sort elementSort;
    Sort setSort;
    TheoryId singletonTheory;
    TypedReasonKind<InSingletonReason> inSingletonReason;
};

struct SingletonSets {
    SingletonSets(Solver&, const SingletonSetsParams&);

    Set singleton(Solver&, Value);
    Value element(Set singletonSet) {
        VERIFY(singletonSet.theory() == params.singletonTheory);
        return singletonInfos[singletonSet].element;
    }

    void propagateContainment(Solver&, SetElement, SetContainment);

    bool testReason(Solver&, Bool, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);

    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

    void checkInvariances(Solver&);

private:
    struct ElementInfo {
        ElementInfo() = default;
        std::optional<Set> singletonSet;
    };
    struct SingletonInfo {
        SingletonInfo() = default;
        Value element = INVALID_VALUE;
    };
    struct ElementState {
        std::optional<Set> singleton;
    };

    Sets& sets(Solver& solver);

    ElementState& stateOf(SetElement elem) {
        if (elem.id() >= elementStates.size()) {
            elementStates.resize(elem.id() + 1);
        }
        return elementStates[elem.id()];
    }

    SingletonSetsParams params;

    std::vector<ElementState> elementStates;

    Trace<SetElement> singletonTrace;

    SortData<ElementInfo> elementInfos;
    TheoryData<SingletonInfo> singletonInfos;
};

}