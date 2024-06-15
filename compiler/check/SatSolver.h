#pragma once

#include <types.h>

#include <bit>

namespace check::sat {

//! A boolean literal
/*!
These are identified by their theory and their id within that theory. For literal X there exist the
complementary literal NOT X. These will always belong to the same theory.
\see Theory::negate()
*/
struct Literal {
    uint32_t theoryId : 8 = -1;
    uint32_t literalId : 24 = -1;

    auto operator<=>(const Literal& other) const {
        return std::pair<uint32_t, uint32_t>(theoryId, literalId)
            <=> std::pair<uint32_t, uint32_t>(other.theoryId, other.literalId);
    }
    bool operator==(const Literal& other) const = default;
};

//! Bitmask type for a clause. Contains 1 bit for each literal in the clause.
using clause_mask_t = uint64_t;
inline constexpr int_t MAX_CLAUSE_SIZE = sizeof(clause_mask_t) * 8;

struct LiteralInstance {
    static constexpr int_t LITERAL_BITS = std::bit_width(sizeof(clause_mask_t) * 8 - 1);
    static constexpr int_t MAX_CLAUSE_INDEX = ((int_t)1 << (32 - LITERAL_BITS)) - 1;

    uint32_t literalIndex : LITERAL_BITS = MAX_CLAUSE_SIZE - 1;
    uint32_t clauseIndex : 32 - LITERAL_BITS = MAX_CLAUSE_INDEX;

    bool operator==(const LiteralInstance&) const = default;
};

//!
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
struct optional_traits<check::sat::Literal> {
    static constexpr check::sat::Literal empty_value = check::sat::Literal();
};

template<>
struct optional_traits<check::sat::LiteralInstance> {
    static constexpr check::sat::LiteralInstance empty_value = check::sat::LiteralInstance();
};

template<>
struct optional_traits<check::sat::TracePosition> {
    static constexpr check::sat::TracePosition empty_value = check::sat::TracePosition(-1);
};

namespace check::sat {

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
        std::span<const Literal> clause;
        int_t forceLiteralIndex = 0;
    };

    //! Test if the reason is still valid
    /*!
    Returns whether the clause this reason is modeling is still forcing.
    */
    virtual bool test(const Reason&) = 0;

    //! Return the clause modeled by this reason
    virtual ClauseAndIndex clause(const Reason&) = 0;

    virtual ~ReasonTheory() = default;
};

struct Theory {
    struct LiteralInfo {
        std::optional<TracePosition> firstReason;
        std::optional<TracePosition> lastReason;

        std::optional<Literal> nextPropagation;
        std::optional<Literal> prevPropagation;

        uint32_t subTraceIndex = -1;
        uint32_t includedInNewClause = -1;

        std::vector<LiteralInstance> instances;

        bool assignedFalse() const { return firstReason.has_value(); }
    };

    virtual void enumerateLiterals(std::function<void(Literal)> visitor) = 0;
    virtual Literal negate(Literal) = 0;
    virtual LiteralInfo* getInfo(Literal) = 0;
    virtual void assignFalse(Literal) = 0;
    virtual void reverseFalseAssignment(Literal) = 0;
    virtual void setTheoryId(int_t id) = 0;
    virtual std::string format(Literal) = 0;
    virtual ~Theory() = default;
};

struct Solver {
    struct SubTraceEntry {
        Literal literal;
        Reason reason;
    };
    using Conflict = SubTraceEntry;

    Solver();

    int_t addTheory(std::unique_ptr<Theory> theory) {
        int_t id = theories.size();
        theory->setTheoryId(id);
        theories.emplace_back(std::move(theory));
        return id;
    }

    Theory* getTheoryById(int_t id) {
        return theories[id].get();
    }

    Theory* theoryFor(Literal literal) {
        return theories[literal.theoryId].get();
    }
    Theory::LiteralInfo* infoFor(Literal literal) {
        return theoryFor(literal)->getInfo(literal);
    }

    ReasonTheory* theoryFor(const Reason& reason) {
        return reasonTheories[reason.reasonTheory].get();
    }

    //! Returns the current decision level of the solver
    /*!
    The decision level is equal to the number of decisions that were made.
    */
    int_t currentLevel() const { return decisions.size(); }

    //! Returns true if \p lit is assigned false and this assignment was propagated to the clause masks
    bool assignedFalseAndPropagated(Literal lit) {
        const auto* info = infoFor(lit);
        if (!info->assignedFalse())
            return false;
        return lit != firstPropagation && !info->prevPropagation.has_value();
    }

    //! Append \p lit the end of the propagation queue
    /*!
    Literals added to the queue should be assigned false at the point of this operation.
    The queue will be processed by propagate() to propagate this assignment to the clause masks.
    */
    void queuePropagation(Literal lit) {
        auto* info = infoFor(lit);
        if (!firstPropagation.has_value()) {
            firstPropagation = lit;
            lastPropagation = lit;
            return;
        }
        info->prevPropagation = lastPropagation.value();
        infoFor(lastPropagation.value())->nextPropagation = lit;
        lastPropagation = lit;
    }

    //! Removes the first item from the propagation queue
    void removeFirstPropagation() {
        VERIFY(firstPropagation.has_value());
        Literal lit = firstPropagation.value();
        auto* info = infoFor(lit);
        firstPropagation = info->nextPropagation;

        if (info->nextPropagation.has_value())
            infoFor(info->nextPropagation.value())->prevPropagation = std::nullopt;
        else
            lastPropagation = std::nullopt;

        info->nextPropagation = std::nullopt;
    }

    //! Removes \p lit from the propagation queue
    void removePropagation(Literal lit) {
        auto* info = infoFor(lit);
        if (info->prevPropagation.has_value())
            infoFor(info->prevPropagation.value())->nextPropagation = info->nextPropagation;
        else
            firstPropagation = info->nextPropagation;

        if (info->nextPropagation.has_value())
            infoFor(info->nextPropagation.value())->prevPropagation = info->prevPropagation;
        else
            lastPropagation = info->prevPropagation;

        info->prevPropagation = std::nullopt;
        info->nextPropagation = std::nullopt;
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
    /*!
    \returns false if \p literal is already known to be false.
    */
    bool decideTrue(Literal literal) {
        VERIFY(!firstPropagation.has_value());
        decisions.push_back(TracePosition(trace.size()));
        return assignTrue(literal, Reason::makeDecision());
    }

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
    The function will the current subTrace that should be from the backtrack() operation that
    resolved \p conflict. When successful the function will identify the UIPs, generate new clauses
    and clear the conflicts. The function will fail if it detects that the solver is still in a
    conflict state. In this case calling propagate() will produce \p conflict again.
    \returns true if successful
    */
    bool tryLearn(Conflict conflict);

    void dumpClause(int_t clauseIndex);
    void dumpClause(const std::vector<Literal>& clause);

    //! Revert all assignments up to and including level
    /*!
    This updates the subTrace member to contain a list of all 1st reasons that are no longer
    forcing in the order they appeared in the original trace. Note this doesn't always mean that
    the literal assignment was also reverted since an other reason may still be forcing.
    */
    void backtrack(int_t targetLevel);

    //! Explicitly check that the invariances of the solver hold
    void checkInvariances();

    //! Check if all clauses are satisfied by the current assignment
    bool checkAssignment();

    bool hasConflicts() const { return !conflicts.empty(); }

private:
    struct ExplicitReasonTheory;

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

    //! Bitmasks for the clauses
    /*!
    The mask will contain a 1 for literals that are not false. That is for all literals that are
    either true or unassigned. If all literals in a clause except one are false, i.e. if this mask
    has exactly one bit set, the remaining literal must be true for the clause to be true.
    Detecting this case is the primary purpose of these masks.

    The updating of the masks is done in propagate() and backtrack(). propagate() will propagate()
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
    The propagation queue is stored as a linked list intrusively inside the Theory::LiteralInfo
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

    std::vector<std::unique_ptr<Theory>> theories;
    std::vector<std::unique_ptr<ReasonTheory>> reasonTheories;

    ExplicitReasonTheory* explicitReasonTheory();
};

}