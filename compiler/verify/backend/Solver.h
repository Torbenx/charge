#pragma once

#include <verify/backend/Data.h>
#include <verify/backend/Reason.h>
#include <verify/backend/Use.h>
#include <verify/backend/Value.h>

namespace verify::backend {

struct DataManager;
struct Sets;

struct Solver {
    static std::unique_ptr<Solver> make();

    SolverImpl& impl();
    const SolverImpl& impl() const;

    int_t currentDecisionLevel() const;
    void backtrack(int_t targetLevel);
    bool assignedTrue(Bool lit);
    bool assignedFalse(Bool lit);
    void decideTrue(Bool literal);
    void assignTrue(Bool trueLit, const Reason& reason);
    bool alwaysTrue(Bool);
    bool alwaysFalse(Bool v) { return alwaysTrue(!v); }

    ClauseBuilder beginClause();
    std::span<const Bool> viewClause(const ClauseBuilder&);
    void addClause(const ClauseBuilder& builder);
    void addClause(std::vector<Bool> clause);

    DataManager& dataManager();
    int_t valueCount(TheoryId);
    int_t booleanCount(TheoryId);
    void forEachValue(TheoryId, auto&& callback);
    void forEachBoolean(TheoryId, auto&& callback);

    //! Create a new value of \p theory
    /*!
    This is a low-level factory function called by the subcomponent managing the values.
    External users should use the specialized factory and expression functions.
    */
    Value newValue(TheoryId);
    //! \see newValue()
    Bool newBoolean(TheoryId);

    //! The set theory of \p sort
    Sets& setTheory(Sort);

    //! Whether \p set is non-empty regardless of the current assignment
    bool alwaysNonEmpty(Set);

    Member composeMembers(std::span<const Member>);
    Member composeMembers(std::initializer_list<Member> expr) {
        return composeMembers((std::span<const Member>)expr);
    }

    std::strong_ordering rewriteOrder(Value, Value);

    PairHandle findPair(Value, Value);
    PairHandle findPair(Pair); // Must be already oriented
    Pair at(PairHandle);

    Bool equality(Value, Value);
    Bool equality(PairHandle);
    bool assignedEqual(Value, Value);

    void addUse(Value, Use);

    //! Propagate that the rewrite of the value referenced by \p use changed
    void propagateRewrite(Use);

    Bool newAuxBooleanVariable();
    Value newAuxUninterpretedConstant();
    Member newAuxMemberVariable();
    Set newAuxUninterpretedConstantSet();
    MemoryDeclaration newAuxMemoryDeclarationVariable();

    //! \p invariants are the invariants of the parent type that use the member
    Member newMemberLiteral(std::vector<Invariant> invariants = {});
    //! The invariants of the parent type that use the member literal
    std::span<const Invariant> usingInvariants(Member literal);

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