#pragma once

#include <verify/backend/Solver.h>

namespace verify::backend {

//! Bitmask type for a clause. Contains 1 bit for each literal in the clause.
using clause_mask_t = uint64_t;
static constexpr int_t MAX_CLAUSE_SIZE = sizeof(clause_mask_t) * 8;

struct LiteralInstance {
    static constexpr int_t LITERAL_BITS = std::bit_width((size_t)MAX_CLAUSE_SIZE - 1);
    static constexpr int_t MAX_CLAUSE_INDEX = ((int_t)1 << (32 - LITERAL_BITS)) - 1;

    uint32_t literalIndex : LITERAL_BITS = MAX_CLAUSE_SIZE - 1;
    uint32_t clauseIndex : 32 - LITERAL_BITS = MAX_CLAUSE_INDEX;

    bool operator==(const LiteralInstance&) const = default;
};

struct Clauses {

    static clause_mask_t literalMask(int_t index) { return (clause_mask_t)1 << index; }

    Clauses(Solver& solver);
    bool testReason(Solver&, BooleanValue assignedLiteral, const Reason& reason);
    ClauseAndIndex reasonToClause(Solver&, BooleanValue assignedLiteral, const Reason& reason);

    void propagateAssignment(Solver&, BooleanValue);
    void unapplyAssignment(Solver&, BooleanValue);
    LiteralInstance asInstance(const Reason& reason);

    void addClause(Solver&, std::vector<BooleanValue> clause);
    void addClauseInternal(Solver&, std::vector<BooleanValue> clause);

    //! Check if all clauses are satisfied by the current assignment
    /*
    If this returns false there is an implementation error.
    The state of the solver must be assumed to be unrecoverable in that case.
    */
    bool checkAssignment(Solver&);

    void checkInvariances(Solver&);

    //! Bitmasks for the clauses
    /*!
    The mask will contain a 1 for literals that are not false. That is for all literals that are
    either true or unassigned. If all literals in a clause except one are false, i.e. if this mask
    has exactly one bit set, the remaining literal must be true for the clause to be true.
    Detecting this case is the primary purpose of these masks.

    The updating of the masks is done in propagate() and backtrack(). propagate() will propagate
    already made false assignments to the masks by clearing the appropiate bits. While backtrack()
    will set the bits for the literals that are detected to be no longer assigned.
    */
    std::vector<clause_mask_t> clauseMasks;

    //! The actual clauses, just arrays of literals
    std::vector<std::vector<BooleanValue>> clauses;

    //! The occurrence map for each literal
    KindData<std::vector<LiteralInstance>> instances;
};

}