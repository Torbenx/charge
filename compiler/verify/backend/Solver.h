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

    bool alwaysTrue(BooleanValue);
    bool alwaysFalse(BooleanValue v) { return alwaysTrue(!v); }
    void addClause(std::vector<BooleanValue> clause);

    int_t valueCount(TheoryId);
    int_t booleanCount(TheoryId);

    BooleanValue newAuxBoolean(std::string name);

    std::vector<BooleanValue>& scratchClause() {
        m_scratchClause.clear();
        return m_scratchClause;
    }

private:
    friend SolverImpl;
    Solver();

    std::vector<BooleanValue> m_scratchClause;
};

}