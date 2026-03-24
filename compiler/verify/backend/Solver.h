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

    std::strong_ordering rewriteOrder(Value, Value);

    PairHandle findPair(Value, Value);
    PairHandle findPair(Pair); // Must be already oriented
    Pair at(PairHandle);

    BooleanValue equality(Value, Value);
    BooleanValue equality(PairHandle);
    bool assignedEqual(Value, Value);

    BooleanValue newAuxBoolean(std::string name);

    /*! \brief Return whether \p a and \p b are always disequal

    If \p a and \p b are always disequal any values less the either \p a or \p b must also be always
    disequal to the other one (and this function must be able to detect this).
    */
    bool alwaysDisequal(Value a, Value b);

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