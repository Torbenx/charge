#pragma once

#include <bit>
#include <map>
#include <queue>
#include <set>
#include <types.h>

namespace check {

//! A boolean literal
/*!
These are identified by their theory and their id within that theory. For literal X there exist the
complementary literal NOT X. These will always belong to the same theory.
\see Theory::negate()
*/
struct Literal {
    static constexpr int_t THEORY_BITS = 4;
    static constexpr int_t MAX_THEORY_COUNT = (int_t)1 << THEORY_BITS;
    static constexpr int_t MAX_LITERAL_ID = ((int_t)1 << (32 - THEORY_BITS)) - 1;

    uint32_t theoryId : THEORY_BITS = MAX_THEORY_COUNT - 1;
    uint32_t literalId : 32 - THEORY_BITS = MAX_LITERAL_ID;

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
struct optional_traits<check::Literal> {
    static constexpr check::Literal empty_value = check::Literal();
};

template<>
struct optional_traits<check::LiteralInstance> {
    static constexpr check::LiteralInstance empty_value = check::LiteralInstance();
};

template<>
struct optional_traits<check::TracePosition> {
    static constexpr check::TracePosition empty_value = check::TracePosition(-1);
};

namespace check {

//! Encapsulates the reason for a literal assignment
/*!
The reason may be either an (external) decision or a clause that forced the assignment. Information
which particular decision lead to the assignment is not of use and is not tacked. For clauses both
the implying clause and the position of the implied variable are stored.
*/
struct Reason {
    std::optional<LiteralInstance> inst;

    static Reason makeDecision() {
        return { std::nullopt };
    }
    static Reason makeClause(LiteralInstance inst) {
        return { inst };
    }

    bool isDecision() const { return !inst.has_value(); }
    int_t clauseIndex() const { return inst.value().clauseIndex; }
    int_t forcedLiteral() const { return inst.value().literalIndex; }
    LiteralInstance asLiteralInstance() const { return inst.value(); }
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

    struct Conflict {
        LiteralInstance assignment;
        Literal impliedLiteralThatsFalse;
    };

    int_t addTheory(std::unique_ptr<Theory> theory) {
        for (auto& theoryPtr : theories) {
            if (theoryPtr == nullptr) {
                theoryPtr = std::move(theory);
                int_t id = &theoryPtr - theories.data();
                theoryPtr->setTheoryId(id);
                return id;
            }
        }
        VERIFY_NOT_REACHED();
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
    resolved \p conflict. When successful the function will identify the 1st UIP, generate a new
    clause and clear the conflicts. The function will fail if it detects that the solver is still
    in a conflict state. In this case calling propagate() will produce \p conflict again.
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

private:
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

    //! Array for the theories
    std::array<std::unique_ptr<Theory>, Literal::MAX_THEORY_COUNT> theories;
};

void Solver::addClauseInternal(std::vector<Literal> clause) {
    VERIFY(!clause.empty());
    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE);
    VERIFY(clauses.size() == clauseMasks.size());
    int_t clauseIndex = clauses.size();
    clause_mask_t mask = 0;
    for (int_t index = 0; index < (int_t)clause.size(); index++) {
        LiteralInstance inst { (uint32_t)index, (uint32_t)clauseIndex };
        Literal lit = clause[index];
        infoFor(lit)->instances.push_back(inst);
        if (!assignedFalseAndPropagated(lit))
            mask |= literalMask(index);
    }
    VERIFY(mask != 0);
    if (std::popcount(mask) == 1) {
        int_t index = std::countr_zero(mask);
        if (!assignTrue(clause[index], Reason::makeClause({ (uint32_t)index, (uint32_t)clauseIndex }))) {
            conflicts.push_back({ LiteralInstance { (uint32_t)index, (uint32_t)clauseIndex }, clause[index] });
        }
    }
    clauses.emplace_back(std::move(clause));
    clauseMasks.push_back(mask);
}

void Solver::addClause(std::vector<Literal> clause) {
    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE * (MAX_CLAUSE_SIZE - 1));
    if ((int_t)clause.size() <= MAX_CLAUSE_SIZE) {
        addClauseInternal(std::move(clause));
        return;
    }
    // clause.size <= (MAX_CLAUSE_SIZE - extraClauses) + extraClauses * (MAX_CLAUSE_SIZE - 1)
    // -> extraClauses >= (clause.size - MAX_CLAUSE_SIZE) / (MAX_CLAUSE_SIZE - 2)
    // -> extraClauses >= floor( (clause.size - MAX_CLAUSE_SIZE + MAX_CLAUSE_SIZE - 3) / (MAX_CLAUSE_SIZE - 2)
    int_t extraClauses = ((int_t)clause.size() - 3) / (MAX_CLAUSE_SIZE - 2);

    // fmt::println("packing {} literals into {} clauses", clause.size(), extraClauses + 1);
    // fmt::print("clause: "); dumpClause(clause);

    int_t takenCount = 0;
    auto take = [&](std::vector<Literal>& into, int_t n) {
        VERIFY((int_t)clause.size() > takenCount);
        n = std::min(n, (int_t)clause.size() - takenCount);
        for (int_t i = 0; i < n; i++)
            into.push_back(clause[takenCount++]);
    };

    std::vector<Literal> primaryClause;
    primaryClause.reserve(MAX_CLAUSE_SIZE);
    take(primaryClause, MAX_CLAUSE_SIZE - extraClauses);

    for (int_t i = 0; i < extraClauses; i++) {
        std::vector<Literal> extraClause;
        extraClause.reserve(MAX_CLAUSE_SIZE);
        auto [posLit, negLit] = makeBooleanPair();
        primaryClause.push_back(posLit);
        extraClause.push_back(negLit);
        take(extraClause, MAX_CLAUSE_SIZE - 1);
        VERIFY(extraClause.size() >= 3);
        // fmt::print("extra: "); dumpClause(extraClause);
        addClauseInternal(std::move(extraClause));
    }

    VERIFY(primaryClause.size() == MAX_CLAUSE_SIZE);
    // fmt::print("primary: "); dumpClause(primaryClause);
    addClauseInternal(std::move(primaryClause));

    VERIFY(takenCount == (int_t)clause.size());
}

bool Solver::assignTrue(Literal trueLit, Reason reason) {
    Theory* theory = theoryFor(trueLit);
    /*if (reason.isDecision())
        fmt::println("deciding {} at level {}", theory->format(trueLit), reason.decisionLevel());
    else
        fmt::println("assigning {} from c{}", theory->format(trueLit), reason.clauseIndex());*/

    Literal falseLit = theory->negate(trueLit);
    auto* info = theory->getInfo(falseLit);

    TracePosition tracePos(trace.size());
    trace.push_back({ falseLit, reason, info->lastReason, std::nullopt });
    if (info->lastReason.has_value())
        at(*info->lastReason).nextReason = tracePos;
    info->lastReason = tracePos;

    if (!info->firstReason.has_value()) {
        info->firstReason = tracePos;
        theory->assignFalse(falseLit);
        queuePropagation(falseLit);
    }

    if (theory->getInfo(trueLit)->assignedFalse())
        return false;
    return true;
}

bool Solver::propagate() {
    if (!conflicts.empty())
        return false;
    while (firstPropagation.has_value()) {
        Literal literal = firstPropagation.value();
        Theory* literalTheory = theoryFor(literal);
        // fmt::println("propagating {}", literalTheory->format(literal));

        checkInvariances();
        const auto* info = literalTheory->getInfo(literal);
        // VERIFY(info->assignedFalse());
        // VERIFY(!literalTheory->getInfo(literalTheory->negate(literal))->assignedFalse());

        removeFirstPropagation();

        // This loop up to the first continue is the hottest part of the solver
        for (auto inst : info->instances) {
            clause_mask_t& clauseMask = clauseMasks[inst.clauseIndex];
            // Perform the popcount before we clear the bit so the operations can be executed in parallel
            int popcnt = std::popcount(clauseMask);

            // VERIFY((clauseMask & literalMask(inst.literalIndex)) != (clause_mask_t)0);
            clauseMask &= ~literalMask(inst.literalIndex);

            // Detect if the clause has only one non-false literal (only one bit set).
            // Since popcnt still counts the bit we just cleared we must test against 2 instead of 1.
            if (popcnt > 2)
                continue;

            VERIFY(popcnt == 2);

            // Unit clause propagation:
            // All other literals in this clause are false thus the last one must be true.
            const auto& clause = clauses[inst.clauseIndex];

            int_t trueLitIndex = std::countr_zero(clauseMask);
            Literal trueLit = clause[trueLitIndex];
            if (!assignTrue(trueLit, Reason::makeClause({ (uint32_t)trueLitIndex, inst.clauseIndex })))
                conflicts.push_back({ LiteralInstance { (uint32_t)trueLitIndex, inst.clauseIndex }, trueLit });
        }
        if (!conflicts.empty())
            return false;
    }
    checkInvariances();
    return true;
}

void Solver::dumpClause(int_t clauseIndex) {
    dumpClause(clauses[clauseIndex]);
}
void Solver::dumpClause(const std::vector<Literal>& clause) {
    for (auto lit : clause)
        std::cout << theoryFor(lit)->format(lit) << " ";
    std::cout << '\n';
}

bool Solver::tryLearn(Conflict conflict) {
    VERIFY(conflicts.empty());
    VERIFY(!subTrace.empty());
    SubTraceEntry conflictDecision = subTrace.front();
    VERIFY(conflictDecision.reason.isDecision());
    if (infoFor(conflictDecision.literal)->assignedFalse()) {
        // if the decision was not reverted, propagating it will lead to a conflict
        return false;
    }
    tryLearnIndex += 1;

    auto wasReversed = [&](Literal lit) {
        return std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; }) != subTrace.end();
    };
    auto wasFalse = [&](Literal lit) { return wasReversed(lit) || infoFor(lit)->assignedFalse(); };
    auto wasTrue = [&](Literal lit) { return wasFalse(theoryFor(lit)->negate(lit)); };

    std::vector<Literal> newClause;
    int_t position = subTrace.size();
    int_t openLiterals = 0;
    int_t uipCount = 0;
    int_t seenClauses = 0;
    std::vector<bool> shouldBeVisited;
    shouldBeVisited.resize(subTrace.size());

    {
        const auto& conflictClause = clauses[conflict.assignment.clauseIndex];
        for (Literal lit : conflictClause) {
            Theory* theory = theoryFor(lit);
            auto* info = theory->getInfo(lit);
            if (info->assignedFalse()) {
                VERIFY(info->includedInNewClause != tryLearnIndex);
                newClause.push_back(lit);
                info->includedInNewClause = tryLearnIndex;
            } else {
                VERIFY(!shouldBeVisited[info->subTraceIndex]);
                openLiterals += 1;
                shouldBeVisited[info->subTraceIndex] = true;
            }
        }
        if (openLiterals == 0) {
            // All literals in the clause were false, this is a conflict.
            return false;
        }
        seenClauses += 1;
    }

    for (;;) {
        for (;;) {
            position -= 1;
            if (shouldBeVisited[position])
                break;
            if (position == 0) {
                // We iterated though the entire trace without seeing a decision.
                // The conflict must still persists.
                return false;
            }
        }

        SubTraceEntry entry = subTrace[position];
        openLiterals -= 1;
        if (openLiterals == 0) {
            // Found a UIP
            uipCount += 1;
            VERIFY(!infoFor(entry.literal)->assignedFalse());

            if (seenClauses > 1) {
                newClause.push_back(entry.literal);
                addClause(std::move(newClause));
                VERIFY(conflicts.empty());
            }

            if (entry.reason.isDecision()) {
                VERIFY(firstPropagation.has_value());
                return true;
            }

            newClause.clear();
            tryLearnIndex += 1;
            Literal negatedUIP = theoryFor(entry.literal)->negate(entry.literal);
            newClause.push_back(negatedUIP);
            infoFor(negatedUIP)->includedInNewClause = tryLearnIndex;
            seenClauses = 0;
        }

        VERIFY(!entry.reason.isDecision());

        seenClauses += 1;
        const auto& clause = clauses[entry.reason.clauseIndex()];
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            Literal lit = clause[index];
            auto* info = infoFor(lit);

            /*if (index != entry.reason.forcedLiteral())
                VERIFY(wasFalse(lit));
            else
                VERIFY(wasTrue(lit));*/

            if (info->assignedFalse()) {
                if (info->includedInNewClause != tryLearnIndex) {
                    newClause.push_back(lit);
                    info->includedInNewClause = tryLearnIndex;
                }
            } else if (index != entry.reason.forcedLiteral()) {
                /*auto it = std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; });
                VERIFY(it != subTrace.end());
                VERIFY(it - subTrace.begin() < position);
                VERIFY(it - subTrace.begin() == (int_t)info->subTraceIndex);*/
                if (!shouldBeVisited[info->subTraceIndex]) {
                    openLiterals += 1;
                    shouldBeVisited[info->subTraceIndex] = true;
                }
            }
        }
    }
}

bool Solver::analyzeConflicts() {
    VERIFY(!conflicts.empty());

    auto doesConflictPersist = [this](Conflict conflict) {
        const auto& mask = clauseMasks[conflict.assignment.clauseIndex];
        bool isClauseForcing = std::popcount(mask) == 1;
        bool isImpliedLiteralFalse = infoFor(conflict.impliedLiteralThatsFalse)->assignedFalse();
        return isClauseForcing && isImpliedLiteralFalse;
    };

    for (;;) {
        Conflict drivingConflict = conflicts.back();

        auto removeResolvedConflcits = [&] {
            while (!conflicts.empty()) {
                if (doesConflictPersist(conflicts.back())) {
                    // Remember the last conflict that persisted
                    drivingConflict = conflicts.back();
                    return;
                }
                conflicts.pop_back();
            }
        };

        // Backtrack until all conflicts are resolved
        while (!conflicts.empty()) {
            if (currentLevel() == 0)
                return false;

            backtrack(currentLevel());
            removeResolvedConflcits();
        }

        // Learn from the last conflict that was resolved
        if (tryLearn(drivingConflict))
            return true;

        // tryLearn() detected that a conflict still persists, find it by propagating
        propagate();
        VERIFY(!conflicts.empty());
    }
}

void Solver::backtrack(int_t targetLevel) {
    VERIFY(targetLevel > 0);
    checkInvariances();
    TracePosition position = decisions[targetLevel - 1];

    subTrace.clear();
    subTrace.reserve(trace.size() - position.index);

    TracePosition writePosition = position;
    TracePosition traceEnd = TracePosition(trace.size());
    for (; position < traceEnd; position++) {
        const TraceEntry entry = at(position);
        Theory* theory = theoryFor(entry.literal);
        auto* info = theory->getInfo(entry.literal);
        // VERIFY(info->firstReason.has_value() && info->lastReason.has_value());

        bool revert = entry.reason.isDecision() || std::popcount(clauseMasks[entry.reason.clauseIndex()]) > 1;
        if (revert) {
            if (info->firstReason.value() == position) {
                // When the first reason is reverted we requeue the propagation.
                info->subTraceIndex = subTrace.size();
                subTrace.push_back({ entry.literal, entry.reason });
                if (entry.nextReason.has_value()) {
                    if (assignedFalseAndPropagated(entry.literal)) {
                        for (auto inst : info->instances) {
                            auto& clauseMask = clauseMasks[inst.clauseIndex];
                            auto mask = literalMask(inst.literalIndex);
                            clauseMask |= mask;
                        }
                        queuePropagation(entry.literal);
                    }

                    // tell nextReason to update firstReason
                    at(*entry.nextReason).prevReason = std::nullopt;
                } else {
                    if (assignedFalseAndPropagated(entry.literal)) {
                        for (auto inst : info->instances) {
                            auto& clauseMask = clauseMasks[inst.clauseIndex];
                            auto mask = literalMask(inst.literalIndex);
                            clauseMask |= mask;
                        }
                    } else {
                        removePropagation(entry.literal);
                    }

                    // revert the literal
                    info->firstReason = std::nullopt;
                    info->lastReason = std::nullopt;
                    theory->reverseFalseAssignment(entry.literal);
                }
            } else {
                if (entry.nextReason.has_value()) {
                    // tell nextReason to update prevReason
                    at(*entry.nextReason).prevReason = entry.prevReason;
                } else if (entry.prevReason.has_value()) {
                    info->lastReason = entry.prevReason;
                    at(*entry.prevReason).nextReason = std::nullopt;
                } else {
                    // revert the literal
                    info->firstReason = std::nullopt;
                    info->lastReason = std::nullopt;
                    theory->reverseFalseAssignment(entry.literal);

                    removePropagation(entry.literal);
                }
            }
        } else {
            *(entry.prevReason.has_value() ? &at(*entry.prevReason).nextReason : &info->firstReason) = writePosition;
            *(entry.nextReason.has_value() ? &at(*entry.nextReason).prevReason : &info->lastReason) = writePosition;

            at(writePosition) = entry;
            writePosition += 1;
        }
    }
    trace.resize(writePosition.index);
    while ((int_t)decisions.size() >= targetLevel)
        decisions.pop_back();
    checkInvariances();
}

void Solver::checkInvariances() {
    return;
    // check reason linked lists
    auto checkLiteral = [this](Literal lit) {
        const auto* info = infoFor(lit);
        if (!info->assignedFalse())
            return;
        VERIFY(info->firstReason.has_value());
        VERIFY(info->lastReason.has_value());
        TracePosition pos = info->firstReason.value();
        VERIFY(!at(pos).prevReason.has_value());
        VERIFY(at(pos).literal == lit);
        // std::cout << theoryFor(lit)->format(lit) << " (" << info->firstReason->index << " .. " << info->lastReason->index << ")" << ": ";
        // std::cout << pos.index;

        while (at(pos).nextReason.has_value()) {
            TracePosition newPos = at(pos).nextReason.value();
            VERIFY(newPos > pos);
            // std::cout << " -> " << newPos.index;
            VERIFY(at(newPos).literal == lit);
            VERIFY(at(newPos).prevReason.has_value());
            VERIFY(at(newPos).prevReason.value() == pos);
            pos = newPos;
        }
        VERIFY(pos == info->lastReason.value());
        // std::cout << '\n';
    };
    for (auto& theory : theories) {
        if (theory == nullptr)
            break;
        theory->enumerateLiterals(checkLiteral);
    }

    // check clause masks
    VERIFY(clauses.size() == clauseMasks.size());
    for (int_t clauseIndex = 0; clauseIndex < (int_t)clauses.size(); clauseIndex++) {
        auto mask = clauseMasks[clauseIndex];
        const auto& clause = clauses[clauseIndex];
        // VERIFY((mask & (literalMask(clause.size()) - (clause_mask_t)1)) == mask);
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            bool bitSet = (mask & literalMask(index)) != 0;
            VERIFY(bitSet == !assignedFalseAndPropagated(clause[index]));
        }
        if (std::popcount(mask) == 1) {
            int_t index = std::countr_zero(mask);
            Literal trueLit = clause[index];
            Theory* theory = theoryFor(trueLit);
            Literal falseLit = theory->negate(trueLit);
            const auto* info = theory->getInfo(falseLit);
            VERIFY(info->assignedFalse());

            VERIFY(info->firstReason.has_value());
            VERIFY(info->lastReason.has_value());
            TracePosition pos = info->firstReason.value();
            for (;;) {
                if (!at(pos).reason.isDecision() && at(pos).reason.clauseIndex() == clauseIndex)
                    break;
                VERIFY(at(pos).nextReason.has_value());
                pos = at(pos).nextReason.value();
            }
            VERIFY(mask == literalMask(at(pos).reason.forcedLiteral()));
            for (Literal lit : clause) {
                if (lit != trueLit) {
                    VERIFY(infoFor(lit)->assignedFalse());
                    VERIFY(infoFor(lit)->firstReason.value() < pos);
                }
            }
        }
    }

    // check decisions
    for (TracePosition pos : decisions) {
        VERIFY(at(pos).reason.isDecision());
    }

    // check propagation linked list
    VERIFY(firstPropagation.has_value() == lastPropagation.has_value());
    if (firstPropagation.has_value()) {
        Literal current = firstPropagation.value();
        VERIFY(!infoFor(current)->prevPropagation.has_value());
        while (infoFor(current)->nextPropagation.has_value()) {
            Literal next = infoFor(current)->nextPropagation.value();
            VERIFY(infoFor(next)->prevPropagation == current);
            current = next;
        }
        VERIFY(current == lastPropagation.value());
    }
}

bool Solver::checkAssignment() {
    for (const auto& clause : clauses) {
        bool foundTrue = false;
        for (Literal lit : clause) {
            Theory* theory = theoryFor(lit);
            if (theory->getInfo(theory->negate(lit))->assignedFalse()) {
                foundTrue = true;
                break;
            }
        }
        if (!foundTrue)
            return false;
    }
    return true;
}

}