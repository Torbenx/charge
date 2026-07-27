#pragma once

#include <verify/backend/Clauses.h>
#include <verify/backend/Data.h>
#include <verify/backend/Value.h>

#include <unordered_set>

namespace verify::backend {

struct SetsParams {
    Sort setSort;
    TheoryId expressionTheory;
    TheoryId emptySetTheory;
    TheoryId equalityTheory;
    TheoryId elementInSetTheory;
    TypedReasonKind<SetClauseDefData> clauseDefToExprReason;
    TypedReasonKind<SetClauseDefData> clauseExprToDefReason;
    TypedReasonKind<LiteralOccurrence> clauseExhaustiveReason;
    TypedReasonKind<SetEqualityToElemData> equalityToElementReason;
    TypedReasonKind<EmptyReasonData> forAllDistribute;
};

namespace theory_params {

#define SET_THEORY(sort, memberName)                                \
    inline constexpr SetsParams sets##sort = {                      \
        Sort::sort,                                                 \
        TheoryId::sort##Expressions,                                \
        TheoryId::sort##EmptySet,                                   \
        TheoryId::sort##Equality,                                   \
        TheoryId::sort##ElementInSet,                               \
        makeTypedReasonKind<ReasonKind::sort##ClauseDefToExpr>(),   \
        makeTypedReasonKind<ReasonKind::sort##ClauseExprToDef>(),   \
        makeTypedReasonKind<ReasonKind::sort##ClauseExhaustive>(),  \
        makeTypedReasonKind<ReasonKind::sort##EqualityToElement>(), \
        makeTypedReasonKind<ReasonKind::sort##ForAllDistribute>(),  \
    };
#include <verify/backend/theories.inc>

}

constexpr bool isSetSort(Sort value) {
    switch (value) {
#define SET_THEORY(sort, memberName) case Sort::sort:
#include <verify/backend/theories.inc>
        return true;
    default:
        return false;
    }
}

struct Sets {
    struct ElementId {
        explicit ElementId(uint32_t id)
            : m_id(id) { }
        uint32_t id() const { return m_id; }

        bool operator==(const ElementId&) const = default;

    private:
        uint32_t m_id = limits::max;
    };

    //! Represents an internal boolean literal of the form '(not) in set'
    struct Containment {
        Containment(Value set, bool contained)
            : theoryBits(std::to_underlying(set.theory()))
            , idBits(set.id())
            , containedBit(contained) { }

        Value set() const { return Value((TheoryId)theoryBits, idBits); }
        bool contained() const { return containedBit != 0u; }
        Containment operator!() const {
            Containment copy = *this;
            copy.containedBit ^= 1u;
            return copy;
        }
        bool operator==(const Containment&) const = default;

    private:
        uint32_t theoryBits : 7;
        uint32_t idBits : 24;
        uint32_t containedBit : 1;
    };
    static Containment in(Value set) { return { set, true }; }

    Sets(Solver&, const SetsParams&);

    Bool makeEquality(PairHandle pair) {
        return { params.equalityTheory, pair.pairId() * 2 };
    }
    Bool isEmpty(Solver& solver, Value value);
    void newPair(Solver&, PairHandle);

    void propagateElementAssignment(Solver&, Bool);
    void unapplyElementAssignment(Solver&, Bool);
    void propagateEquality(Solver&, PairHandle);

    bool testReason(Solver&, Bool, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);

    bool assignedTrue(Solver&, ElementId, Containment);
    bool assignedFalse(Solver& solver, ElementId element, Containment literal) {
        return assignedTrue(solver, element, !literal);
    }
    void assignTrue(Solver&, ElementId, Containment, const Reason&);
    void decideTrue(Solver&, ElementId, Containment);

    bool assignedEmpty(Solver& solver, Value);

    Bool mapToBool(Solver&, ElementId, Containment);
    std::optional<Bool> tryToBool(Solver&, ElementId, Containment);
    std::pair<ElementId, Containment> mapFromBool(Bool);

    ElementId newElement(Solver& solver);

    Value emptySet() { return Value(params.emptySetTheory, 0); }
    Sort setSort() const { return params.setSort; }

    Value union_(Solver&, std::span<const Value>);
    Value union_(Solver& solver, std::initializer_list<Value> vals) {
        return union_(solver, { vals.begin(), vals.end() });
    }

    Value intersection(Solver& solver, std::span<const Value> vals) {
        std::array<Value, 0> minusArr {};
        return subset(solver, { vals.begin(), vals.end() }, minusArr);
    }
    Value intersection(Solver& solver, std::initializer_list<Value> vals) {
        return intersection(solver, { vals.begin(), vals.end() });
    }

    Value setminus(Solver& solver, Value base, std::span<const Value> minus) {
        std::array<Value, 1> baseArr { base };
        return subset(solver, baseArr, minus);
    }
    Value setminus(Solver& solver, Value base, std::initializer_list<Value> minus) { return setminus(solver, base, { minus.begin(), minus.end() }); }

    Value subset(Solver&, std::span<const Value> intersection, std::span<const Value> minus);
    Value subset(Solver& solver, std::initializer_list<Value> intersection, std::initializer_list<Value> minus) {
        return subset(solver, { intersection.begin(), intersection.end() }, { minus.begin(), minus.end() });
    }

    void refineClause(Solver&, std::vector<Bool>& clause);

private:
    struct ElementInfo {
        //! Masks tracking the assignments in the expression clauses
        /*!
        Contains a 1 bit for each literal that is not false.
        Therefore if only 1 bit remains that literal must be propagated.
        */
        std::vector<clause_mask_t> clauseMasks;
    };

    struct LiteralInfo {
        uint32_t inClause = limits::max;
        std::vector<LiteralOccurrence> occurrences;
    };

    struct EqualityInfo {
        PairHandle pair;
        Value otherSet;
    };

    struct SetInfo {
        std::array<LiteralInfo, 2> literalInfos = {};
        std::optional<Bool> isEmptyLiteral;
        std::vector<EqualityInfo> equalities;
        std::vector<std::optional<Bool>> elementInSetLiterals;
    };

    struct ElementInInfo {
        ElementId element = ElementId(limits::max);
        Value set = INVALID_VALUE;
    };

    struct HashLookup {
        size_t hash;
        Sets& sets;
        std::span<const Containment> clause;
    };

    struct HashEntry {
        Value expr;
        size_t hash;
    };

    struct ClauseHash {
        using is_transparent = void;

        size_t operator()(const HashEntry& entry) const { return entry.hash; }
        size_t operator()(const HashLookup& lookup) const { return lookup.hash; }
    };

    struct ClauseHashEqual {
        using is_transparent = void;

        bool operator()(const HashEntry& a, const HashEntry& b) const {
            return a.expr == b.expr;
        }
        bool operator()(const HashLookup& a, const HashEntry& b) const;
    };

    ElementId forAllElement() const { return ElementId(0); }

    bool unionExpression(Value) const;
    bool subsetExpression(Value) const;

    Value addClause(Solver&, std::vector<Containment>);

    void propagateContainment(Solver&, ElementId, Containment);
    void unapplyContainment(Solver&, ElementId, Containment);

    LiteralInfo& infoFor(Containment lit) {
        return setInfos[lit.set()].literalInfos[lit.contained()];
    }

    SetsParams params;

    uint32_t nextClauseAttempt = 0;

    //! Represents union and subset expressions
    /*!
    There are two kinds of expressions:
    - Union expressions of the from union(a_1, ..., a_n)
    - Subset expression of the from setminus(intersection(a_1, ... a_m), b_1, ..., b_n)

    The containment propagation rules for unions are:
    u1: in union(a_1, ..., a_n) and not in a_i for all i != k => in a_k
    u2: not in a_i for all i => not in union(a_1, ..., a_n)
    u3: in a_i for any i => in union(a_1, ..., a_n)
    u4: not in union(a_1, ..., a_n) => not in a_i for all i

    And for subset expressions the rules are:
    s1a: not in setminus(intersection(a_1, ... a_m), b_1, ..., b_n) and in a_i for all i and not in b_j for all j != k => in b_k
    s1b: not in setminus(intersection(a_1, ... a_m), b_1, ..., b_n) and in a_i for all i != k and not in b_j for all j => not in a_k
    s2:  in a_i for all i and not in b_j for all j => in setminus(intersection(a_1, ... a_m), b_1, ..., b_n)
    s3a: not in a_i for any i => not in setminus(intersection(a_1, ... a_m), b_1, ..., b_n)
    s3b: in b_j for any j => not in setminus(intersection(a_1, ... a_m), b_1, ..., b_n)
    s4:  in setminus(intersection(a_1, ... a_m), b_1, ..., b_n) => in a_i for all i and not in b_j for all j

    We unify these by using 'clauses' of containment literals. The clauses are defined to be:
    [not in union(a_1, ..., a_n), in a_1, ..., in a_n]
    [in setminus(intersection(a_1, ... a_m), b_1, ..., b_n), not in a_1, ..., not in a_m, in b_1, ..., in b_n]

    The rules for the clauses are:
    - If all except one literal in the clause is false the last literal is true. This handles rules u1, u2, s1a, s1b and s2.
    - If any literal other than the first is true the first literal is false. This handles rules u3, s3a and s3b.
    - If the first literal is true than all other literals are false. This handles rules u4 and s4.
    */
    std::vector<std::vector<Containment>> clauses;
    std::unordered_set<HashEntry, ClauseHash, ClauseHashEqual> clauseSet;

    std::vector<ElementInfo> elements;

    SortData<SetInfo> setInfos;
    TheoryData<ElementInInfo, TheoryId::COUNT, 2> inSetInfos;
};

}