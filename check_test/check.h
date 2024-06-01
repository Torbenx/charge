#pragma once

#include <bit>
#include <map>
#include <queue>
#include <set>
#include <types.h>

namespace check {

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

using clause_mask_t = uint32_t;
inline constexpr int_t MAX_CLAUSE_SIZE = sizeof(clause_mask_t) * 8;

struct LiteralInstance {
    static constexpr int_t LITERAL_BITS = std::bit_width(sizeof(clause_mask_t) * 8 - 1);
    uint32_t literalIndex : LITERAL_BITS;
    uint32_t clauseIndex : 32 - LITERAL_BITS;

    bool operator==(const LiteralInstance&) const = default;
};

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
struct optional_traits<check::TracePosition> {
    static constexpr check::TracePosition empty_value = check::TracePosition(-1);
};

namespace check {

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

        std::optional<int_t> subTraceIndex;

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
        bool shouldBeVisited = false;
    };

    Solver() {
        decisions.push_back(TracePosition(0));
    }

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

    int_t currentLevel() const { return decisions.size() - 1; }

    bool assignedFalseAndPropagated(Literal lit) {
        const auto* info = infoFor(lit);
        if (!info->assignedFalse())
            return false;
        return lit != firstPropagation && !info->prevPropagation.has_value();
    }

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

    void dequeuePropagation(Literal lit) {
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

    void addClauseInternal(std::vector<Literal> clause);

    std::pair<Literal, Literal> makeBooleanPair();

    void addClause(std::vector<Literal> clause);

    bool decideTrue(Literal literal) {
        VERIFY(!firstPropagation.has_value());
        decisions.push_back(TracePosition(trace.size()));
        return assignTrue(literal, Reason::makeDecision());
    }

    bool assignTrue(Literal trueLit, Reason reason);

    bool propagate();

    bool learnClause();

    void dumpClause(int_t clauseIndex);
    void dumpClause(const std::vector<Literal>& clause);

    // Revert all assignments up to and including level
    void backtrack(int_t targetLevel, std::vector<SubTraceEntry>& subTrace);

    void validateMasks();

    void checkAssignment();

private:
    struct Conflict {
        LiteralInstance assignment;
    };

    struct TraceEntry {
        Literal literal;
        Reason reason;
        // Form a linked list for trace entries for 'literal'
        std::optional<TracePosition> prevReason;
        std::optional<TracePosition> nextReason;
    };

    static clause_mask_t literalMask(int_t index) { return (clause_mask_t)1 << index; }

    TraceEntry& at(TracePosition pos) {
        VERIFY(pos.index < trace.size());
        return trace[pos.index];
    }

    // Bitmasks for learned clauses. Contains 1 for literals that are not assigned 'false'.
    std::vector<clause_mask_t> learnedClauseMasks;

    // The actual learned clauses
    std::vector<std::vector<Literal>> learnedClauses;

    // Trace of reasons
    std::vector<TraceEntry> trace;

    std::optional<Literal> firstPropagation;
    std::optional<Literal> lastPropagation;

    std::vector<TracePosition> decisions;

    std::array<std::unique_ptr<Theory>, Literal::MAX_THEORY_COUNT> theories;

    std::vector<Conflict> conflicts;
};

void Solver::addClauseInternal(std::vector<Literal> clause) {
    VERIFY(!clause.empty());
    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE);
    VERIFY(learnedClauses.size() == learnedClauseMasks.size());
    int_t clauseIndex = learnedClauses.size();
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
            conflicts.push_back({ LiteralInstance { (uint32_t)index, (uint32_t)clauseIndex } });
        }
    }
    learnedClauses.emplace_back(std::move(clause));
    learnedClauseMasks.push_back(mask);
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
    take(primaryClause, MAX_CLAUSE_SIZE - extraClauses);

    for (int_t i = 0; i < extraClauses; i++) {
        std::vector<Literal> extraClause;
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
        at(info->lastReason.value()).nextReason = tracePos;
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
    while (firstPropagation.has_value()) {
        Literal literal = firstPropagation.value();
        Theory* literalTheory = theoryFor(literal);
        // fmt::println("propagating {}", literalTheory->format(literal));

        validateMasks();
        const auto* info = literalTheory->getInfo(literal);
        VERIFY(info->assignedFalse());
        VERIFY(!literalTheory->getInfo(literalTheory->negate(literal))->assignedFalse());

        dequeuePropagation(literal);

        for (auto inst : info->instances) {
            clause_mask_t& clauseMask = learnedClauseMasks[inst.clauseIndex];
            int popcnt = std::popcount(clauseMask);
            VERIFY((clauseMask & literalMask(inst.literalIndex)) != (clause_mask_t)0);
            clauseMask &= ~literalMask(inst.literalIndex);
            if (popcnt > 2)
                continue;

            VERIFY(popcnt == 2);

            // Unit clause propagation:
            // All other literals in this clause are 'false' thus the last one must be 'true'.
            const auto& clause = learnedClauses[inst.clauseIndex];

            int_t trueLitIndex = std::countr_zero(clauseMask);
            Literal trueLit = clause[trueLitIndex];
            if (assignTrue(trueLit, Reason::makeClause({ (uint32_t)trueLitIndex, inst.clauseIndex })))
                continue;

            conflicts.push_back({ LiteralInstance { (uint32_t)trueLitIndex, inst.clauseIndex } });
        }
        if (!conflicts.empty())
            return false;
    }
    validateMasks();
    return true;
}

void Solver::dumpClause(int_t clauseIndex) {
    dumpClause(learnedClauses[clauseIndex]);
}
void Solver::dumpClause(const std::vector<Literal>& clause) {
    for (auto lit : clause)
        std::cout << theoryFor(lit)->format(lit) << " ";
    std::cout << '\n';
}

bool Solver::learnClause() {
    VERIFY(!conflicts.empty());

    int_t conflictIndex = 0;
    auto doesConflictPersist = [&](Conflict conflict) {
        auto [assignment] = conflict;
        const auto& clause = learnedClauses[assignment.clauseIndex];
        const auto& mask = learnedClauseMasks[assignment.clauseIndex];
        Literal literal = clause[assignment.literalIndex];
        bool isClauseForcing = std::popcount(mask) == 1;
        if (isClauseForcing)
            VERIFY(mask == literalMask(assignment.literalIndex));
        bool isImpliedLiteralFalse = theoryFor(literal)->getInfo(literal)->assignedFalse();
        return isClauseForcing && isImpliedLiteralFalse;
    };
    auto doesSomeConflictPersist = [&] {
        if (doesConflictPersist(conflicts[conflictIndex]))
            return true;
        for (int_t index = conflictIndex + 1; index < (int_t)conflicts.size(); index++) {
            if (doesConflictPersist(conflicts[index])) {
                conflictIndex = index;
                return true;
            }
        }
        return false;
    };

    std::vector<SubTraceEntry> subTrace;
    for (;;) {
        validateMasks();
        VERIFY(doesSomeConflictPersist());
        conflictIndex = 0;

        while (doesSomeConflictPersist() && currentLevel() > 0) {
            subTrace.clear();
            backtrack(currentLevel(), subTrace);
        }

        if (currentLevel() == 0 && doesSomeConflictPersist())
            return false;

        VERIFY(!subTrace.empty());
        SubTraceEntry conflictDecision = subTrace.front();
        VERIFY(conflictDecision.reason.isDecision());
        if (infoFor(conflictDecision.literal)->assignedFalse()) {
            // if the decision was not reverted propagating it will to a conflict
            conflicts.clear();
            propagate();
            VERIFY(!conflicts.empty());
            continue;
        }

        auto wasReversed = [&](Literal lit) {
            return std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; }) != subTrace.end();
        };
        auto wasFalse = [&](Literal lit) { return wasReversed(lit) || infoFor(lit)->assignedFalse(); };
        auto wasTrue = [&](Literal lit) { return wasFalse(theoryFor(lit)->negate(lit)); };

        std::vector<Literal> newClause;
        int_t position = subTrace.size();
        int_t openLiterals = 0;
        int_t seenDecisions = 0;
        LiteralInstance clauseReason = conflicts[conflictIndex].assignment;
        Literal forcedConflictLiteral = learnedClauses[clauseReason.clauseIndex][clauseReason.literalIndex];
        Literal alreadyFalseConflictLiteral = theoryFor(forcedConflictLiteral)->negate(forcedConflictLiteral);
        // VERIFY(wasReversed(forcedConflictLiteral));
        VERIFY(wasFalse(alreadyFalseConflictLiteral));

        do {
            const auto& clause = learnedClauses[clauseReason.clauseIndex];
            // fmt::print("visiting ");
            // dumpClause(clause);
            for (int_t index = 0; index < (int_t)clause.size(); index++) {
                Literal lit = clause[index];
                Theory* theory = theoryFor(lit);
                auto* info = theory->getInfo(lit);

                /*if (index != clauseReason.literalIndex) {
                    VERIFY(wasFalse(lit));
                } else {
                    VERIFY(wasTrue(lit));
                    bool isConflictLit = lit == forcedConflictLiteral || lit == alreadyFalseConflictLiteral;
                    VERIFY(isConflictLit == wasFalse(lit));
                }*/

                if (info->assignedFalse()) {
                    newClause.push_back(lit);
                } else {
                    if (index != clauseReason.literalIndex) {
                        VERIFY(lit != forcedConflictLiteral && lit != alreadyFalseConflictLiteral);
                        /*auto it = std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; });
                        VERIFY(it != subTrace.end());
                        VERIFY(it - subTrace.begin() < position);
                        VERIFY(it - subTrace.begin() == info->subTraceIndex.value());*/
                        SubTraceEntry& entry = subTrace[info->subTraceIndex.value()];
                        if (!entry.shouldBeVisited) {
                            entry.shouldBeVisited = true;
                            openLiterals += 1;
                        }
                    } else if (lit == forcedConflictLiteral || lit == alreadyFalseConflictLiteral) {
                        if (info->subTraceIndex.value() < position) {
                            SubTraceEntry& entry = subTrace[info->subTraceIndex.value()];
                            if (!entry.shouldBeVisited) {
                                entry.shouldBeVisited = true;
                                openLiterals += 1;
                            }
                        }
                    }
                }
            }

            position -= 1;
            for (; position >= 0; position--) {
                SubTraceEntry entry = subTrace[position];
                if (entry.shouldBeVisited) {
                    openLiterals -= 1;
                    if (entry.reason.isDecision() || openLiterals == 0) {
                        VERIFY(openLiterals == 0);
                        seenDecisions += 1;
                        newClause.push_back(entry.literal);
                        position = -1;
                        break;
                    } else {
                        clauseReason = entry.reason.asLiteralInstance();
                        break;
                    }
                }
            }
        } while (position >= 0);
        if (seenDecisions == 0) {
            conflicts.clear();
            propagate();
            VERIFY(!conflicts.empty());
            continue;
        }
        VERIFY(seenDecisions == 1);
        VERIFY(openLiterals == 0);

        std::sort(newClause.begin(), newClause.end());
        auto newEnd = std::unique(newClause.begin(), newClause.end());
        newClause.erase(newEnd, newClause.end());

        // fmt::print("learned: ");
        // dumpClause(newClause);

        conflicts.clear();
        addClause(std::move(newClause));
        if (!conflicts.empty())
            return learnClause();

        VERIFY(firstPropagation.has_value());
        return true;
    }
}

void Solver::backtrack(int_t targetLevel, std::vector<SubTraceEntry>& subTrace) {
    VERIFY(targetLevel > 0);
    validateMasks();
    TracePosition position = decisions[targetLevel];
    TracePosition writePosition = position;
    TracePosition traceEnd = TracePosition(trace.size());
    for (; position < traceEnd; position++) {
        const TraceEntry entry = at(position);

        Theory* theory = theoryFor(entry.literal);
        auto* info = theory->getInfo(entry.literal);
        VERIFY(info->firstReason.has_value() && info->lastReason.has_value());

        bool revert = entry.reason.isDecision() || std::popcount(learnedClauseMasks[entry.reason.clauseIndex()]) > 1;
        if (revert) {
            if (info->firstReason.value() == position) {
                if (assignedFalseAndPropagated(entry.literal)) {
                    for (auto inst : info->instances) {
                        auto& clauseMask = learnedClauseMasks[inst.clauseIndex];
                        auto mask = literalMask(inst.literalIndex);
                        clauseMask |= mask;
                    }
                    queuePropagation(entry.literal);
                }
                info->subTraceIndex = subTrace.size();
                subTrace.push_back({ entry.literal, entry.reason });
            }
            if (entry.nextReason.has_value()) {
                // tell nextReason to update prevReason
                at(entry.nextReason.value()).prevReason = entry.prevReason;
            } else if (entry.prevReason.has_value()) {
                info->lastReason = entry.prevReason.value();
                at(entry.prevReason.value()).nextReason = std::nullopt;
            } else {
                // revert the literal
                info->firstReason = std::nullopt;
                info->lastReason = std::nullopt;
                theory->reverseFalseAssignment(entry.literal);

                dequeuePropagation(entry.literal);
            }
        } else {
            if (entry.prevReason.has_value())
                at(entry.prevReason.value()).nextReason = writePosition;
            else
                info->firstReason = writePosition;

            if (entry.nextReason.has_value())
                at(entry.nextReason.value()).prevReason = writePosition;
            else
                info->lastReason = writePosition;

            at(writePosition) = entry;
            writePosition += 1;
        }
    }
    trace.resize(writePosition.index);
    while ((int_t)decisions.size() > targetLevel)
        decisions.pop_back();
    validateMasks();
}

void Solver::validateMasks() {
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
    VERIFY(learnedClauses.size() == learnedClauseMasks.size());
    for (int_t clauseIndex = 0; clauseIndex < (int_t)learnedClauses.size(); clauseIndex++) {
        auto mask = learnedClauseMasks[clauseIndex];
        const auto& clause = learnedClauses[clauseIndex];
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
    for (int_t i = 1; i < (int_t)decisions.size(); i++) {
        VERIFY(at(decisions[i]).reason.isDecision());
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

void Solver::checkAssignment() {
    for (const auto& clause : learnedClauses) {
        bool foundTrue = false;
        for (Literal lit : clause) {
            Theory* theory = theoryFor(lit);
            if (theory->getInfo(theory->negate(lit))->assignedFalse()) {
                foundTrue = true;
                break;
            }
        }
        VERIFY(foundTrue);
    }
}

}