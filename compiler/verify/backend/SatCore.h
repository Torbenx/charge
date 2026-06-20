#pragma once

#include <verify/backend/Solver.h>

namespace verify::backend {

struct TracePosition {
    uint32_t index;

    constexpr explicit TracePosition(uint32_t index)
        : index(index) { }

    auto operator<=>(const TracePosition&) const = default;
    bool operator==(const TracePosition&) const = default;
    TracePosition& operator++() {
        index += 1;
        return *this;
    }
    TracePosition operator++(int) {
        TracePosition copy = *this;
        index += 1;
        return copy;
    }
    friend TracePosition operator+(TracePosition l, int_t r) {
        return TracePosition(l.index + r);
    }
    friend TracePosition operator-(TracePosition l, int_t r) {
        return TracePosition(l.index - r);
    }
    TracePosition& operator+=(int_t r) {
        index += r;
        return *this;
    }
    TracePosition& operator-=(int_t r) {
        index -= r;
        return *this;
    }
};

}

template<>
struct optional_traits<verify::backend::TracePosition> {
    static constexpr verify::backend::TracePosition empty_value = verify::backend::TracePosition(limits::max);
};

namespace verify::backend {

struct DecisionData {
    uint32_t decisionLevel;
};

struct SatCore {
    using Literal = BooleanValue;

    struct LiteralInfo {
        std::optional<TracePosition> firstReason;
        std::optional<TracePosition> lastReason;

        std::optional<Literal> nextPropagation;
        std::optional<Literal> prevPropagation;

        uint32_t subTraceIndex = limits::max;
        uint32_t lastContainingClauseId = limits::max;
        uint32_t lastContainingLearnClauseId = limits::max;

        bool tentativelyTrue() const { return firstReason.has_value(); }
    };

    struct Interface {
        LiteralInfo& infoFor(Literal);

        void onNewDecisionLevel();
        void onBeginBacktrack();
        void onEndBacktrack();

        bool testReason(Literal, const Reason&);
        ClauseAndIndex reasonToClause(Literal, const Reason&);

        void propagateAssignment(Literal);
        void unapplyAssignment(Literal);

        void learnClause(std::vector<BooleanValue>);
    };

    ClauseBuilder beginClause() {
        ClauseBuilder r(nextClauseId);
        nextClauseId += 1;
        wipClause.clear();
        return r;
    }
    bool addToClause(const ClauseBuilder& b, Literal lit) {
        VERIFY(b.clauseId == nextClauseId - 1);
        auto& info = infoFor(lit);
        if (info.lastContainingClauseId != b.clauseId) {
            wipClause.push_back(lit);
            info.lastContainingClauseId = b.clauseId;
            return true;
        }
        return false;
    }
    std::span<const Literal> viewClause(const ClauseBuilder& b) {
        VERIFY(b.clauseId == nextClauseId - 1);
        return wipClause;
    }

    //! Returns the current decision level of the solver
    /*!
    The decision level is equal to the number of decisions that were made minus 1.

    A level of -1 implies that no decisions were made. If a conflict is found at this level the
    problem is unsatisfiable.
    */
    int_t currentDecisionLevel() const { return (int_t)decisions.size() - 1; }

    Reason firstReason(Literal lit);
    ClauseAndIndex justifyAssignment(Literal lit);

    //! Returns true if \p lit is assigned true and this assignment was propagated to the theories
    bool assignedTrue(Literal lit);

    bool alwaysTrue(Literal lit);

    bool assignedFalse(Literal lit) { return assignedTrue(!lit); }

    //! Decide that the given \p literal is true
    void decideTrue(Literal literal);

    //! Assign true to \p trueLit
    /*!
    This called either by decideTrue() or internally, for example by propagate() or addClause().
    */
    void assignTrue(Literal trueLit, const Reason& reason);

    //! Propagate the assignments in the propagation queue to the clause masks
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

    //! Revert all assignments up to and including \p level
    /*!
    This updates the subTrace member to contain a list of all 1st reasons that are no longer
    forcing in the order they appeared in the original trace. Note this doesn't always mean that
    the literal assignment was also reverted since another reason may still be forcing.
    */
    void beginBacktrack(int_t level);

    void endBacktrack();

    //! Explicitly check that the invariances of the solver hold
    void checkInvariances();

    //! Return whether the solver has any conflicts
    bool hasConflicts() const { return !conflicts.empty(); }

private:
    struct SubTraceEntry {
        Literal literal;
        Reason reason;
    };
    using Conflict = SubTraceEntry;

    //! An entry in the trace
    /*!
    Each entry represent a reason for a literal assignment. The different reasons for a given literal
    are also linked together into a linked list.
    */
    struct TraceEntry {
        Literal literal; //!< Literal that is true
        Reason reason; //!< Reason the literal was assigned
        std::optional<TracePosition> prevReason; //!< The previous reason for the assignment in trace order
        std::optional<TracePosition> nextReason; //!< The next reason for the assignment in trace order
    };

    Interface& interface();

    LiteralInfo& infoFor(Literal lit) { return interface().infoFor(lit); }

    TraceEntry& at(TracePosition pos) {
        VERIFY(pos.index < trace.size());
        return trace[pos.index];
    }

    bool addToLearnClause(Literal lit);

    //! Try to learn a new clause from \p conflict
    /*!
    The function uses the current subTrace that should be from the backtrack() operation that
    resolved \p conflict. When successful the function will identify the UIPs, generate new clauses
    and clear the conflicts. The function will fail if it detects that the solver is still in a
    conflict state. In this case calling propagate() will produce a conflict again.
    \returns the list of clauses learned and a bool indicating if the function was successful
    */
    std::pair<std::vector<std::vector<Literal>>, bool> tryLearn(Conflict conflict);

    //! Append \p lit the end of the propagation queue
    /*!
    Literals added to the queue should be assigned true at the point of this operation.
    The queue will be processed by propagate() to propagate this assignment to the clause masks.
    */
    void queuePropagation(Literal lit);

    //! Removes the first item from the propagation queue
    void removeFirstPropagation();

    //! Removes \p lit from the propagation queue
    void removePropagation(Literal lit);

    //! Collect all reason for \p trueLit to be true
    std::vector<Reason> collectReasons(Literal trueLit);

    //! Trace of reasons
    std::vector<TraceEntry> trace;

    //! First element in the propagation queue
    /*!
    The propagation queue is stored as a linked list intrusively inside the BooleanTheory::LiteralInfo
    (members nextPropagation and prevPropagation). It conatins the pending assignments to be
    processed by propagate().
    */
    std::optional<Literal> firstPropagation;
    //! Last element in the propagation queue
    /*! \see firstPropagation */
    std::optional<Literal> lastPropagation;

    bool backtracking = false;

    uint32_t nextClauseId = 0;
    std::vector<Literal> wipClause;

    uint32_t learnClauseId = 0;
    std::vector<Literal> learnClause;

    std::vector<Conflict> conflicts;
    std::vector<SubTraceEntry> subTrace;

    //! Positions of the decisions in the trace
    std::vector<TracePosition> decisions;
};

}