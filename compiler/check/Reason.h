#pragma once

#include <check/Value.h>

#include <span>

namespace check {

struct Solver;

//! Encapsulates the reason for a literal assignment
/*!
The reason may be either an (external) decision or a clause that forced the assignment. A clause
may be either an explicit clause of the boolean formula or lazily generated only when needed.
*/
struct Reason {
    static constexpr int_t INVALID_THEORY_ID = 255;
    uint32_t reasonTheory : 8 = 0;
    uint32_t data0 : 24 = 0;
    uint32_t data1 = 0;
    uint32_t data2 = 0;

    static Reason makeDecision(int_t decisionLevel) {
        return { .reasonTheory = INVALID_THEORY_ID, .data1 = (uint32_t)decisionLevel };
    }

    bool isDecision() const { return reasonTheory == INVALID_THEORY_ID; }
    int_t decisionLevel() const {
        VERIFY(isDecision());
        return data1;
    }
};

struct ReasonTheory {
    struct ClauseAndIndex {
        std::span<const BooleanValue> clause;
        int_t forceLiteralIndex = 0;
    };

    //! Constructor
    /*!
    \param solver the solver this reason theory will belong to
    \param propagating indication to the solver whether this propagating theory. This should be set
            to true if any reason that the theory could ever produce will automatically propagate.
    */
    ReasonTheory(Solver& solver, bool propagating);
    virtual ~ReasonTheory() = default;
    ReasonTheory(const ReasonTheory&) = delete;
    ReasonTheory(ReasonTheory&&) = delete;
    ReasonTheory& operator=(const ReasonTheory&) = delete;
    ReasonTheory& operator=(ReasonTheory&&) = delete;

    //! Test if the reason is still valid
    /*!
    Returns whether the clause this reason is modeling is still forcing.
    */
    virtual bool testReason(Solver&, const Reason&) = 0;

    //! Return the clause modeled by this reason
    virtual ClauseAndIndex reasonToClause(Solver&, const Reason&) = 0;

    //! Called by the solver when a new decision level starts
    /*!
    The theory must be able to backtrack to the current state later.
    */
    virtual void newDecisionLevel(Solver&) = 0;

    //! Backtrack to a previous state
    /*!
    There are two approaches to implement backtracking:
    1) By relying on BooleanTheory::unapplyFalseAssignment() to revert assignments only when
       required. This may have better performance due to fewer operations being performed but is
       not always applicable.
    2) By implementing backtrack() to revert to a previous state and using
       BooleanTheory::reapplyFalseAssignment() to reapply assignment that were falsely reverted.
       This is easier to achive since BooleanTheory::reapplyFalseAssignment() performs an
       operation very similar to BooleanTheory::propagateFalseAssignment() which must be supported
       in either case.

    If approach 2) is used the theory should backtrack to the state is was in when newDecisionLevel()
    was called with Solver::currentDecisionLevel(). The decision level after this call should be
    equal to the solver's decision level.
    */
    virtual void backtrack(Solver&) = 0;

    int_t theoryId() const { return m_theoryId; }
    bool isPropagating() const { return m_propagating; }

private:
    uint8_t m_theoryId;
    bool m_propagating;
};

}