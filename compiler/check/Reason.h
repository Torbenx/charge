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

    static Reason makeDecision() {
        return { .reasonTheory = INVALID_THEORY_ID };
    }

    bool isDecision() const { return reasonTheory == INVALID_THEORY_ID; }
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
            to true if any reason that is theory could ever produce will automatically propagate.
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

    int_t theoryId() const { return m_theoryId; }
    bool isPropagating() const { return m_propagating; }

private:
    uint8_t m_theoryId;
    bool m_propagating;
};

}