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
    void backtrack(int_t targetLevel);
    bool assignedTrue(BooleanValue lit);
    bool assignedFalse(BooleanValue lit);
    void decideTrue(BooleanValue literal);
    void assignTrue(BooleanValue trueLit, const Reason& reason);
    bool alwaysTrue(BooleanValue);
    bool alwaysFalse(BooleanValue v) { return alwaysTrue(!v); }

    ClauseBuilder beginClause();
    std::span<const BooleanValue> viewClause(const ClauseBuilder&);
    void addClause(const ClauseBuilder& builder);
    void addClause(std::vector<BooleanValue> clause);

    int_t valueCount(TheoryId);
    int_t booleanCount(TheoryId);
    void forEachValue(TheoryId, auto&& callback);
    void forEachBoolean(TheoryId, auto&& callback);

    Member composeMembers(std::span<const Member>);
    Member composeMembers(std::initializer_list<Member> expr) {
        return composeMembers((std::span<const Member>)expr);
    }

    std::strong_ordering rewriteOrder(Value, Value);

    PairHandle findPair(Value, Value);
    PairHandle findPair(Pair); // Must be already oriented
    Pair at(PairHandle);

    BooleanValue equality(Value, Value);
    BooleanValue equality(PairHandle);
    bool assignedEqual(Value, Value);

    BooleanValue newAuxBooleanVariable();
    Value newAuxUninterpretedConstant();
    Member newAuxMemberVariable();
    Value newAuxUninterpretedConstantSet();

    /*! \brief Return whether \p a and \p b are always disequal

    If \p a and \p b are always disequal any values less the either \p a or \p b must also be always
    disequal to the other one (and this function must be able to detect this).
    */
    bool alwaysDisequal(Value a, Value b);

private:
    friend SolverImpl;
    Solver();
};

}