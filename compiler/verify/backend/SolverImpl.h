#pragma once

#include <verify/backend/Clauses.h>
#include <verify/backend/DataManager.h>
#include <verify/backend/Members.h>
#include <verify/backend/PairSet.h>
#include <verify/backend/SatCore.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/UninterpretedEquality.h>
#include <verify/backend/SingletonSets.h>

#include <ReverseMemberPointer.h>

namespace verify::backend {

struct SolverImpl : Solver, SatCore::Interface {
    struct BuiltinTrueFalse {
        BuiltinTrueFalse(Solver&);
    };
    struct AlwaysReason {
        bool testReason(Solver&, Bool, const Reason&);
        ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);
    };
    struct DecisionReason {
        bool testReason(Solver&, Bool, const Reason&);
        ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);
    };

    SolverImpl();

    Value newValue(TheoryId);
    Bool newBoolean(TheoryId);

    void onNewBooleanPair(PairHandle);
    void onNewPair(PairHandle);
    template<TheoryId theory>
    uint64_t pairLabelOf(Value v);

    Sets& setTheory(Sort);
    void propagateSetContainment(Sets&, Sets::ElementId, Sets::Containment);
    bool setAlwaysNonEmpty(Value);

    // The initialization order here matters:

    // Initialize members with trivial ctors
    AlwaysReason alwaysReason;
    DecisionReason decisionReason;

    // DataManager must be initialized first, everything depends on it
    DataManager data;

    // Setup literal infos before SatCore
    SortData<SatCore::LiteralInfo, Sort::Boolean> literalInfos;
    // Initialize SatCore and Clauses next, some theories may perform assignments during construction
    SatCore sat;
    Clauses clauses;
    // Now we can construct the builtin true and false literals, also likely to be used in other theories
    BuiltinTrueFalse builtinTrueFalse;

    std::array<PairSet, std::to_underlying(Sort::COUNT)> pairs;

    UninterpretedEquality uninterpConstantEquality;

    Members members;

    Sets uninterpConstantSets;
    SingletonSets uninterpConstantSingletons;
};

inline SolverImpl& Solver::impl() {
    return static_cast<SolverImpl&>(*this);
}
inline const SolverImpl& Solver::impl() const {
    return static_cast<const SolverImpl&>(*this);
}

inline void Solver::forEachValue(TheoryId theory, auto&& callback) {
    int_t valueCount = this->valueCount(theory);
    for (int_t i = 0; i < valueCount; i++)
        callback(Value(theory, i));
}

inline void Solver::forEachBoolean(TheoryId theory, auto&& callback) {
    int_t boolCount = this->booleanCount(theory);
    for (int_t i = 0; i < boolCount; i++)
        callback(Bool(theory, i * 2));
}

}