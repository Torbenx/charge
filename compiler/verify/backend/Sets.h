#pragma once

#include <verify/backend/Clauses.h>
#include <verify/backend/Data.h>
#include <verify/backend/Value.h>

#include <unordered_set>

namespace verify::backend {

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

    Sets(Solver&,
        TheoryId expressionTheory,
        TheoryId emptySetTheory,
        TheoryId equalityTheory,
        TheoryId isEmptyTheory,
        TheoryId elementInSetTheory);

    BooleanValue makeEquality(PairHandle pair) {
        return { equalityTheory, pair.pairId() * 2 };
    }
    BooleanValue makeIsEmpty(Solver& solver, Value value);
    void newPair(Solver&, PairHandle);

    void propagateElementAssignment(Solver&, BooleanValue);
    void unapplyElementAssignment(Solver&, BooleanValue);
    void propagateEquality(Solver&, PairHandle);
    void propagateIsEmpty(Solver&, BooleanValue);

    bool testReason(Solver&, BooleanValue, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&);

    bool assignedTrue(Solver&, ElementId, Containment);
    bool assignedFalse(Solver& solver, ElementId element, Containment literal) {
        return assignedTrue(solver, element, !literal);
    }
    void assignTrue(Solver&, ElementId, Containment, const Reason&);
    void decideTrue(Solver&, ElementId, Containment);

    ElementId newElement(Solver& solver);

    Value emptySet() { return Value(emptySetTheory, 0); }

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

    void refineClause(Solver&, std::vector<BooleanValue>& clause);

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
        std::optional<BooleanValue> isEmptyLiteral;
        std::vector<EqualityInfo> equalities;
        std::vector<std::optional<BooleanValue>> elementInSetLiterals;
    };

    struct ElementInInfo {
        ElementId element = ElementId(limits::max);
        Value set = INVALID_VALUE;
    };

    struct IsEmptyInfo {
        Value set;
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

    bool unionExpression(Value) const;
    bool subsetExpression(Value) const;

    Value addClause(Solver&, std::vector<Containment>);

    void propagateContainment(Solver&, ElementId, Containment);
    void unapplyContainment(Solver&, ElementId, Containment);

    LiteralInfo& infoFor(Containment lit) {
        return setInfos[lit.set()].literalInfos[lit.contained()];
    }

    BooleanValue mapToBool(Solver&, ElementId, Containment);
    std::pair<ElementId, Containment> mapFromBool(BooleanValue);

    ValueKind setKind;
    TheoryId expressionTheory;
    TheoryId emptySetTheory;
    TheoryId equalityTheory;
    TheoryId isEmptyTheory;
    TheoryId elementInSetTheory;

    uint32_t nextClauseAttempt = 0;

    //! Represents union and subset expressions
    /*!
    There are two kinds of expressions:
    - Union expressions of the from union(a_1, ..., a_n) where the a_i are subset expressions or variables
    - Subset expression of the from setminus(intersection(A_1, ... A_m), B_1, ..., B_n) where the A_i and B_j are variables

    The containment propagation rules for unions are:
    u1: in union(a_1, ..., a_n) and not in a_i for all i != k => in a_k
    u2: not in a_i for all i => not in union(a_1, ..., a_n)
    u3: in a_i for any i => in union(a_1, ..., a_n)
    u4: not in union(a_1, ..., a_n) => not in a_i for all i

    And for subset expressions the rules are:
    s1a: not in setminus(intersection(A_1, ... A_m), B_1, ..., B_n) and in A_i for all i and not in B_j for all j != k => in B_k
    s1b: not in setminus(intersection(A_1, ... A_m), B_1, ..., B_n) and in A_i for all i != k and not in B_j for all j => not in A_k
    s2:  in A_i for all i and not in B_j for all j => in setminus(intersection(A_1, ... A_m), B_1, ..., B_n)
    s3a: not in A_i for any i => not in setminus(intersection(A_1, ... A_m), B_1, ..., B_n)
    s3b: in B_j for any j => not in setminus(intersection(A_1, ... A_m), B_1, ..., B_n)
    s4:  in setminus(intersection(A_1, ... A_m), B_1, ..., B_n) => in A_i for all i and not in B_j for all j

    We unify these by using 'clauses' of containment literals. The clauses are defined to be:
    [not in union(a_1, ..., a_n), in a_1, ..., in a_n]
    [in setminus(intersection(A_1, ... A_m), B_1, ..., B_n), not in A_1, ..., not in A_m, in B_1, ..., in B_n]

    The rules for the clauses are:
    - If all except one literal in the clause is false the last literal is true. This handles rules u1, u2, s1a, s1b and s2.
    - If any literal other than the first is true the first literal is false. This handles rules u3, s3a and s3b.
    - If the first literal is true than all other literals are false. This handles rules u4 and s4.
    */
    std::vector<std::vector<Containment>> clauses;
    std::unordered_set<HashEntry, ClauseHash, ClauseHashEqual> clauseSet;

    std::vector<ElementInfo> elements;

    KindData<SetInfo> setInfos;
    TheoryData<ElementInInfo, TheoryId::COUNT, 2> inSetInfos;
    TheoryData<IsEmptyInfo, TheoryId::COUNT, 2> isEmptyInfos;
};

struct SetClauseDefData {
    Sets::Containment def;
    Sets::Containment expr;
};

struct SetEqualityToElemData {
    PairHandle pair;
    Sets::Containment source;
};

struct SetEmptyToElemData {
    BooleanValue isEmptyLiteral;
};

}