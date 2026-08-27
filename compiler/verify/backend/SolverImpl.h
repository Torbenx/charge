#pragma once

#include <verify/backend/Clauses.h>
#include <verify/backend/DataManager.h>
#include <verify/backend/DecisionDriver.h>
#include <verify/backend/InvariantSets.h>
#include <verify/backend/Members.h>
#include <verify/backend/MemorySets.h>
#include <verify/backend/PairSet.h>
#include <verify/backend/SatCore.h>
#include <verify/backend/Sets.h>
#include <verify/backend/SingletonSets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/UninterpretedEquality.h>
#include <verify/backend/Use.h>

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
    //! Records the notifications of UseKind::Test, so that the mechanism itself can be tested
    struct UseTest {
        void propagateRewrite(Solver&, Use use) { rewrites.push_back(use); }

        std::vector<Use> rewrites;
    };

    SolverImpl();

    void onNewBooleanPair(PairHandle);
    void onNewPair(PairHandle);
    template<TheoryId theory>
    uint64_t pairLabelOf(Value v);

    // The initialization order here matters:

    // Initialize members with trivial ctors
    AlwaysReason alwaysReason;
    DecisionReason decisionReason;
    UseTest useTest;

    // DataManager must be initialized first, everything depends on it
    DataManager data;

    // Data instances cannot reach any theories but may be accessed by them
    SortData<SatCore::LiteralInfo, Sort::Boolean> literalInfos;
    TheoryData<std::vector<Invariant>, TheoryId::MemberLiterals> memberLiteralInvariants;

    // Initialize SatCore and Clauses next, some theories may perform assignments during construction
    SatCore sat;
    Clauses clauses;
    // The driver observes the assignments of the theories, so it has to exist before they do
    DecisionDriver decisionDriver;
    // Now we can construct the builtin true and false literals, also likely to be used in other theories
    BuiltinTrueFalse builtinTrueFalse;

    std::array<PairSet, std::to_underlying(Sort::COUNT)> pairs;

    UninterpretedEquality uninterpConstantEquality;

    Members members;

    Sets uninterpConstantSets;
    SingletonSets uninterpConstantSingletons;

    UninterpretedEquality memoryDeclarationEquality;

    Sets memorySetsBaseTheory;
    MemorySets memorySets;

    Sets invariantSetsBaseTheory;
    InvariantSets invariantSets;
};

inline SolverImpl& Solver::impl() {
    return static_cast<SolverImpl&>(*this);
}
inline const SolverImpl& Solver::impl() const {
    return static_cast<const SolverImpl&>(*this);
}

}