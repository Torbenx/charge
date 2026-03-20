#pragma once

#include <verify/backend/DataManager.h>
#include <verify/backend/SatCore.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Clauses.h>

#include <ReverseMemberPointer.h>

namespace verify::backend {

struct SolverImpl : Solver, SatCore::Interface {
    struct BuiltinTrueFalse {
        BuiltinTrueFalse(Solver&);
    };
    struct AlwaysReason {
        bool testReason(Solver&, BooleanValue, const Reason&);
        ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&);
    };
    struct DecisionReason {
        bool testReason(Solver&, BooleanValue, const Reason&);
        ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&);
    };

    SolverImpl();

    Value newValue(TheoryId);
    BooleanValue newBoolean(TheoryId);

    // The initialization order here matters:

    // Initialize members with trivial ctors
    AlwaysReason alwaysReason;
    DecisionReason decisionReason;

    // DataManager must be initialized first, everything depends on it
    DataManager data;

    // Setup literal infos before SatCore
    KindData<SatCore::LiteralInfo> literalInfos;
    // Initialize SatCore and Clauses next, some theories may perform assignments during construction
    SatCore sat;
    Clauses clauses;
    // Now we can construct the builtin true and false literals, also likely to be used in other theories
    BuiltinTrueFalse builtinTrueFalse;

    TheoryData<std::string, 2> auxBoolNames;
};

inline SolverImpl& Solver::impl() {
    return static_cast<SolverImpl&>(*this);
}
inline const SolverImpl& Solver::impl() const {
    return static_cast<const SolverImpl&>(*this);
}

}