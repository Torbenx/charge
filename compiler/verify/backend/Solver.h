#pragma once

#include <verify/backend/Data.h>
#include <verify/backend/Reason.h>
#include <verify/backend/Value.h>

namespace verify::backend {

struct Solver {
    static std::unique_ptr<Solver> make();

    SolverImpl& impl();
    const SolverImpl& impl() const;

    int_t currentDecisionLevel() const;
    bool assignedTrue(BooleanValue lit);
    bool assignedFalse(BooleanValue lit);
    void decideTrue(BooleanValue literal);
    void assignTrue(BooleanValue trueLit, const Reason& reason);

    int_t valueCount(TheoryId);
    int_t booleanCount(TheoryId);

    BooleanValue newAuxBoolean(std::string name);

private:
    friend SolverImpl;
    Solver();
};

}