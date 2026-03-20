#pragma once

#include <verify/backend/DataManager.h>
#include <verify/backend/SatCore.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Clauses.h>

#include <ReverseMemberPointer.h>

namespace verify::backend {

struct SolverImpl : Solver, SatCore::Interface {
    SolverImpl();

    Value newValue(TheoryId);
    BooleanValue newBoolean(TheoryId);

    DataManager data;

    KindData<SatCore::LiteralInfo> literalInfos;
    SatCore sat;

    Clauses clauses;

    TheoryData<std::string, 2> auxBoolNames;
};

inline SolverImpl& Solver::impl() {
    return static_cast<SolverImpl&>(*this);
}
inline const SolverImpl& Solver::impl() const {
    return static_cast<const SolverImpl&>(*this);
}

}