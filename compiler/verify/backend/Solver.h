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

    //! A solver together with a reference naming it
    /*!
    Use as:

        auto [solver, _] = Solver::makeReference();
    */
    static std::pair<Solver&, std::unique_ptr<Solver>> makeReference();

    virtual ~Solver();

    SolverImpl& impl();
    const SolverImpl& impl() const;

    int_t currentDecisionLevel() const;
    void backtrack(int_t targetLevel);
    void beginBacktrack(int_t targetLevel);
    void endBacktrack();
    bool assignedTrue(Bool lit);
    bool assignedFalse(Bool lit);
    void decideTrue(Bool literal);
    void assignTrue(Bool trueLit, const Reason& reason);
    bool alwaysTrue(Bool);
    bool alwaysFalse(Bool v) { return alwaysTrue(!v); }

    bool propagate();
    bool hasConflicts() const;
    bool analyzeConflicts();

    Reason firstReason(Bool lit);
    ClauseAndIndex justifyAssignment(Bool lit);

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

    // The set operations below take the sort of their theory from the sets they are given,
    // so they are usable without naming the set theory involved.

    SetElement newSetElement(Sort);
    Set emptySet(Sort);

    //! The literal saying that \p set holds no elements at all
    Bool isEmpty(Set);
    bool assignedEmpty(Set);

    Set union_(std::span<const Set>);
    Set union_(std::initializer_list<Set> vals) { return union_(std::span<const Set>(vals)); }

    Set intersection(std::span<const Set>);
    Set intersection(std::initializer_list<Set> vals) { return intersection(std::span<const Set>(vals)); }

    Set setminus(Set base, std::span<const Set> minus);
    Set setminus(Set base, std::initializer_list<Set> minus) { return setminus(base, std::span<const Set>(minus)); }

    Set subset(std::span<const Set> intersection, std::span<const Set> minus);
    Set subset(std::initializer_list<Set> intersection, std::initializer_list<Set> minus) {
        return subset(std::span<const Set>(intersection), std::span<const Set>(minus));
    }

    bool assignedTrue(SetElement, SetContainment);
    bool assignedFalse(SetElement element, SetContainment literal) {
        return assignedTrue(element, !literal);
    }
    void assignTrue(SetElement, SetContainment, const Reason&);
    void decideTrue(SetElement, SetContainment);

    //! The boolean literal the containment is represented by, creating it when needed
    Bool mapToBool(SetElement, SetContainment);

    //! Propagate \p containment to the theory defining its set
    void propagateSetContainment(Sets&, SetElement, SetContainment);

    //! The set holding \p element and nothing else
    Set singleton(Value element);
    //! The element of the singleton set \p set, the inverse of singleton()
    Value singletonElement(Set set);

    //! The set of the memory described by \p location
    MemorySet memorySet(MemoryLocation location);
    MemorySet memorySet(MemoryDeclaration declaration, Member member) {
        return memorySet({ declaration, member });
    }
    //! The location \p set describes, the inverse of memorySet()
    MemoryLocation locationOf(MemorySet set);

    //! The invariants of \p location and of its members
    InvariantSet inclusiveInvariantSet(MemoryLocation location);
    InvariantSet inclusiveInvariantSet(MemoryDeclaration declaration, Member member) {
        return inclusiveInvariantSet({ declaration, member });
    }
    //! The invariants of the members of \p location, but not of the location itself
    InvariantSet exclusiveInvariantSet(MemoryLocation location);
    InvariantSet exclusiveInvariantSet(MemoryDeclaration declaration, Member member) {
        return exclusiveInvariantSet({ declaration, member });
    }
    //! The invariants of the locations strictly above \p location
    InvariantSet pathInvariantSet(MemoryLocation location);
    InvariantSet pathInvariantSet(MemoryDeclaration declaration, Member member) {
        return pathInvariantSet({ declaration, member });
    }
    //! The set holding \p invariant of \p location and nothing else
    InvariantSet invariantSingletonSet(MemoryLocation location, Invariant invariant);
    InvariantSet invariantSingletonSet(MemoryDeclaration declaration, Member member, Invariant invariant) {
        return invariantSingletonSet({ declaration, member }, invariant);
    }

    //! The location \p set describes
    MemoryLocation locationOf(InvariantSet set);
    //! The invariant of the singleton set \p set
    Invariant invariantOf(InvariantSet set);

    Member composeMembers(std::span<const Member>);
    Member composeMembers(std::initializer_list<Member> expr) {
        return composeMembers((std::span<const Member>)expr);
    }

    //! The normal form of \p m, i.e. its defining expression with all rewrites applied
    std::vector<Member> memberRewrite(Member m);
    //! Append the normal form of \p m to \p out
    void appendMemberRewrite(Member m, std::vector<Member>& out);
    //! Justify the normal form of \p m by adding the negated reasons to \p clause
    void explainMemberRewrite(Member m, ClauseBuilder& clause);

    std::strong_ordering rewriteOrder(Value, Value);

    PairHandle findPair(Value, Value);
    PairHandle findPair(Pair); // Must be already oriented
    Pair at(PairHandle);

    Bool equality(Value, Value);
    Bool equality(PairHandle);
    bool assignedEqual(Value, Value);
    void explainEqual(Value a, Value b, ClauseBuilder& clause);

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

    //! Explicitly check that the invariances of all theories hold
    void checkInvariances();

private:
    friend SolverImpl;
    Solver();
};

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