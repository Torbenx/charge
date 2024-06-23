#pragma once

#include <check/Reason.h>
#include <check/BooleanVariables.h>

#include <bit>

namespace check {

struct Solver {
    using Literal = BooleanValue;

    struct SubTraceEntry {
        Literal literal;
        Reason reason;
    };
    using Conflict = SubTraceEntry;

    Solver();

    ValueTheory& getTheoryById(int_t id) {
        return *valueTheories[id];
    }

    ValueTheory& theoryFor(Value value) {
        return *valueTheories[value.theoryId];
    }
    BooleanTheory& theoryFor(BooleanValue value) {
        return static_cast<BooleanTheory&>(*valueTheories[value.theoryId]);
    }
    BooleanTheory::LiteralInfo& infoFor(BooleanValue literal) {
        return theoryFor(literal).literalInfo(*this, literal);
    }

    ReasonTheory& theoryFor(const Reason& reason) {
        return *reasonTheories[reason.reasonTheory];
    }

    std::string formatValue(Value value) {
        return theoryFor(value).formatValue(*this, value);
    }

    uint64_t labelOf(Value value) {
        return theoryFor(value).labelOf(*this, value);
    }

    Literal negate(Literal lit) {
        return theoryFor(lit).negate(*this, lit);
    }

    bool assignedFalse(Literal lit) {
        return theoryFor(lit).literalInfo(*this, lit).assignedFalse();
    }

    bool assignedTrue(Literal lit) {
        auto& theory = theoryFor(lit);
        return theory.literalInfo(*this, theory.negate(*this, lit)).assignedFalse();
    }

    std::strong_ordering compare(Value a, Value b) { return labelOf(a) <=> labelOf(b); }

    //! Returns the current decision level of the solver
    /*!
    The decision level is equal to the number of decisions that were made minus 1.

    A level of -1 implies that no decision were made. If a conflict is found at this level the
    problem is unsatisfiable.
    */
    int_t currentDecisionLevel() const { return (int_t)decisions.size() - 1; }

    //! Returns true if \p lit is assigned false and this assignment was propagated to the clause masks
    bool assignedFalseAndPropagated(Literal lit) {
        const auto& info = infoFor(lit);
        if (!info.assignedFalse())
            return false;
        return lit != firstPropagation && !info.prevPropagation.has_value();
    }

    //! Append \p lit the end of the propagation queue
    /*!
    Literals added to the queue should be assigned false at the point of this operation.
    The queue will be processed by propagate() to propagate this assignment to the clause masks.
    */
    void queuePropagation(Literal lit) {
        auto& info = infoFor(lit);
        if (!firstPropagation.has_value()) {
            firstPropagation = lit;
            lastPropagation = lit;
            return;
        }
        info.prevPropagation = lastPropagation.value();
        infoFor(lastPropagation.value()).nextPropagation = lit;
        lastPropagation = lit;
    }

    //! Removes the first item from the propagation queue
    void removeFirstPropagation() {
        VERIFY(firstPropagation.has_value());
        Literal lit = firstPropagation.value();
        auto& info = infoFor(lit);
        firstPropagation = info.nextPropagation;

        if (info.nextPropagation.has_value())
            infoFor(info.nextPropagation.value()).prevPropagation = std::nullopt;
        else
            lastPropagation = std::nullopt;

        info.nextPropagation = std::nullopt;
    }

    //! Removes \p lit from the propagation queue
    void removePropagation(Literal lit) {
        auto& info = infoFor(lit);
        if (info.prevPropagation.has_value())
            infoFor(info.prevPropagation.value()).nextPropagation = info.nextPropagation;
        else
            firstPropagation = info.nextPropagation;

        if (info.nextPropagation.has_value())
            infoFor(info.nextPropagation.value()).prevPropagation = info.prevPropagation;
        else
            lastPropagation = info.prevPropagation;

        info.prevPropagation = std::nullopt;
        info.nextPropagation = std::nullopt;
    }

    //! Helper for adding clauses
    /*!
    The size of \p clause must be less or equal to MAX_CLAUSE_SIZE.
    */
    void addClauseInternal(std::vector<Literal> clause);

    //! Add a clause to the problem
    /*!
    The size of \p clause exceeds MAX_CLAUSE_SIZE it will be broken down into smaller ones by
    introducing auxilliary variables.
    */
    void addClause(std::vector<Literal> clause);

    //! Make a pair of boolean literals (X, NOT X)
    std::pair<Literal, Literal> makeBooleanPair();

    //! Decide that the given \p literal is true
    void decideTrue(Literal literal);

    //! Assign true to \p trueLit
    /*!
    This called either by decideTrue() or internally, for example by propagate() or addClause().
    */
    bool assignTrue(Literal trueLit, Reason reason);

    //! Propagate the false assignments in the propagation queue to the clause masks
    /*!
    \returns true if all assignements were successfully propagated or false if a conflict arose
             during the propagation.
    */
    bool propagate();

    //! Analyze the current conflicts
    /*!
    This function will backtrack until all conflicts are resolved and learn a clause from them. If
    the conflicts cannot be resolved the problem is unsatisfiable and false is returned.
    */
    bool analyzeConflicts();

    //! Try to learn a new clause from \p conflict
    /*!
    The function uses the current subTrace that should be from the backtrack() operation that
    resolved \p conflict. When successful the function will identify the UIPs, generate new clauses
    and clear the conflicts. The function will fail if it detects that the solver is still in a
    conflict state. In this case calling propagate() will produce a conflict again.
    \returns true if successful
    */
    bool tryLearn(Conflict conflict);

    void dumpClause(int_t clauseIndex);
    void dumpClause(const std::vector<Literal>& clause);

    //! Revert all assignments up to and including \p level
    /*!
    This updates the subTrace member to contain a list of all 1st reasons that are no longer
    forcing in the order they appeared in the original trace. Note this doesn't always mean that
    the literal assignment was also reverted since another reason may still be forcing.
    */
    void backtrack(int_t level);

    //! Explicitly check that the invariances of the solver hold
    void checkInvariances();

    //! Check if all clauses are satisfied by the current assignment
    bool checkAssignment();

    //! Return whether the solver has any conflicts
    bool hasConflicts() const { return !conflicts.empty(); }

    std::vector<BooleanValue>& scratchClause() {
        m_scratchClause.clear();
        return m_scratchClause;
    }

private:
    struct ExplicitReasons : ReasonTheory {
        ExplicitReasons(Solver& solver);
        bool testReason(Solver&, const Reason& reason) override;
        ClauseAndIndex reasonToClause(Solver&, const Reason& reason) override;
        void newDecisionLevel(Solver&) override;
        void backtrack(Solver&) override;

        void propagateFalseAssignment(Solver&, BooleanValue);
        void reapplyFalseAssignment(Solver&, BooleanValue);
        void unapplyFalseAssignment(Solver&, BooleanValue);
        LiteralInstance asInstance(const Reason& reason);
    };

    //! An entry in the trace
    /*!
    Each entry represent a reason for a literal to false. The different reasons for a given literal
    are also linked together into a linked list.
    */
    struct TraceEntry {
        Literal literal; //!< Literal that is false
        Reason reason; //!< Reason the literal was assigned false
        std::optional<TracePosition> prevReason; //!< The previous reason for the assignment in trace order
        std::optional<TracePosition> nextReason; //!< The next reason for the assignment in trace order
    };

    static clause_mask_t literalMask(int_t index) { return (clause_mask_t)1 << index; }

    TraceEntry& at(TracePosition pos) {
        VERIFY(pos.index < trace.size());
        return trace[pos.index];
    }

    Reason makeClauseReason(int_t clauseIndex, int_t literalIndex);

    friend ValueTheory;
    //! Attch a new theory to the solver
    /*!
    Called by the ValueTheory constructor.
    \returns the theory id for \p theory
    */
    int_t attachTheory(ValueTheory& theory);

    friend ReasonTheory;
    //! Attch a new theory to the solver
    /*!
    Called by the ReasonTheory constructor.
    \returns the theory id for \p theory
    */
    int_t attachTheory(ReasonTheory& theory);

    //! Scratch space to hold a temporary clause
    /*!
    This is useful for reason theories that lazily generate clauses.
    */
    std::vector<BooleanValue> m_scratchClause;

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
    std::vector<std::vector<Literal>> clauses;

    //! Trace of reasons
    std::vector<TraceEntry> trace;

    //! First element in the propagation queue
    /*!
    The propagation queue is stored as a linked list intrusively inside the BooleanTheory::LiteralInfo
    (members nextPropagation and prevPropagation). It conatins the pending false assignments to be
    processed by propagate().
    */
    std::optional<Literal> firstPropagation;
    //! Last element in the propagation queue
    /*! \see firstPropagation */
    std::optional<Literal> lastPropagation;

    std::vector<Conflict> conflicts;
    std::vector<SubTraceEntry> subTrace;

    //! Used to mark literals seen by the current learn attempt
    uint32_t tryLearnIndex = 0;

    //! Positions of the decisions in the trace
    std::vector<TracePosition> decisions;

    std::vector<ValueTheory*> valueTheories;
    std::vector<ReasonTheory*> reasonTheories;

    // --- These variables must be initialized last since their constructors modify the theory arrays ---

    BooleanVariables internalVariables;
    ExplicitReasons explicitReasons;
};

}