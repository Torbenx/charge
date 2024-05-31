#pragma once

#include <bit>
#include <map>
#include <queue>
#include <set>
#include <types.h>

namespace check {

struct Literal {
    static constexpr int_t THEORY_BITS = 4;
    static constexpr int_t MAX_THEORY_COUNT = (1 << THEORY_BITS);
    uint32_t theoryId : THEORY_BITS;
    uint32_t literalId : 32 - THEORY_BITS;

    auto operator<=>(const Literal& other) const {
        return std::pair<uint32_t, uint32_t>(theoryId, literalId)
            <=> std::pair<uint32_t, uint32_t>(other.theoryId, other.literalId);
    }
    bool operator==(const Literal& other) const = default;
};

using clause_mask_t = uint64_t;
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
struct optional_traits<check::TracePosition> {
    static constexpr check::TracePosition empty_value = check::TracePosition(-1);
};

namespace check {

struct ClauseOrDecision {
    uint32_t decisionBit : 1 = 0;
    uint32_t data : 31 = 0;

    static ClauseOrDecision makeDecision(int_t decisionLevel) {
        return { 1, (uint32_t)decisionLevel };
    }
    static ClauseOrDecision makeClause(int_t clauseIndex) {
        return { 0, (uint32_t)clauseIndex };
    }

    bool isDecision() const { return decisionBit != 0; }
    int_t decisionLevel() const {
        VERIFY(isDecision());
        return data;
    }
    int_t clauseIndex() const {
        VERIFY(!isDecision());
        return data;
    }
};

struct Theory {
    struct LiteralInfo {
        std::optional<TracePosition> firstReason;
        std::optional<TracePosition> lastReason;
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
    struct LiteralAndReason {
        Literal literal;
        ClauseOrDecision reason;
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

    int_t currentLevel() const { return decisions.size() - 1; }

    bool assignedFalseAndPropagated(Literal lit) {
        return theoryFor(lit)->getInfo(lit)->assignedFalse() && !unpropagatedLiterals.contains(lit);
    }

    void addClauseInternal(std::vector<Literal> clause) {
        VERIFY(!clause.empty());
        VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE);
        VERIFY(learnedClauses.size() == learnedClauseMasks.size());
        int_t clauseIndex = learnedClauses.size();
        clause_mask_t mask = 0;
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            LiteralInstance inst { (uint32_t)index, (uint32_t)clauseIndex };
            Literal lit = clause[index];
            Theory* theory = theoryFor(lit);
            theory->getInfo(lit)->instances.push_back(inst);
            if (!assignedFalseAndPropagated(lit))
                mask |= literalMask(index);
        }
        VERIFY(mask != 0);
        if (std::popcount(mask) == 1) {
            int_t index = std::countr_zero(mask);
            if (!assignTrue(clause[index], ClauseOrDecision::makeClause(clauseIndex))) {
                if (!conflict.has_value())
                    conflict = Conflict { LiteralInstance { (uint32_t)index, (uint32_t)clauseIndex } };
            }
        }
        learnedClauses.emplace_back(std::move(clause));
        learnedClauseMasks.push_back(mask);
    }

    std::pair<Literal, Literal> makeBooleanPair();

    void addClause(std::vector<Literal> clause) {
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

    bool decideTrue(Literal literal) {
        VERIFY(unpropagatedLiterals.empty());
        decisions.push_back(TracePosition(trace.size()));
        return assignTrue(literal, ClauseOrDecision::makeDecision(currentLevel()));
    }

    bool assignTrue(Literal trueLit, ClauseOrDecision reason) {
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
            unpropagatedLiterals.emplace(falseLit);
        }

        if (theory->getInfo(trueLit)->assignedFalse())
            return false;
        return true;
    }

    bool propagate() {
        while (!unpropagatedLiterals.empty()) {
            Literal literal = *unpropagatedLiterals.begin();
            Theory* literalTheory = theoryFor(literal);
            // fmt::println("propagating {}", literalTheory->format(literal));

            validateMasks();
            const auto* info = literalTheory->getInfo(literal);
            VERIFY(info->assignedFalse());

            unpropagatedLiterals.erase(*unpropagatedLiterals.begin());

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
                if (assignTrue(trueLit, ClauseOrDecision::makeClause(inst.clauseIndex)))
                    continue;

                if (!conflict.has_value())
                    conflict = Conflict { LiteralInstance { (uint32_t)trueLitIndex, inst.clauseIndex } };
            }
            if (conflict.has_value())
                return false;
        }
        validateMasks();
        return true;
    }

    bool learnClause() {
        VERIFY(conflict.has_value());

        const auto& conflictClause = learnedClauses[conflict->assignment.clauseIndex];
        const auto& mask = learnedClauseMasks[conflict->assignment.clauseIndex];
        Literal conflictLiteral = conflictClause[conflict->assignment.literalIndex];

        auto doesConflictPersist = [&] {
            bool isClauseForcing = std::popcount(mask) == 1;
            if (isClauseForcing)
                VERIFY(mask == literalMask(conflict->assignment.literalIndex));
            bool isImpliedLiteralFalse = theoryFor(conflictLiteral)->getInfo(conflictLiteral)->assignedFalse();
            return isClauseForcing && isImpliedLiteralFalse;
        };

        validateMasks();
        VERIFY(doesConflictPersist());

        std::vector<LiteralAndReason> reversedLiterals;
        while (doesConflictPersist() && currentLevel() > 0) {
            reversedLiterals.clear();
            backtrack(currentLevel(), reversedLiterals);
        }

        if (currentLevel() == 0 && doesConflictPersist())
            return false;

        std::map<Literal, std::optional<ClauseOrDecision>> reasons;
        for (auto e : reversedLiterals)
            reasons.emplace(e.literal, e.reason);

        std::vector<int_t> clausesToVisit;
        std::vector<Literal> newClause;
        clausesToVisit.push_back(conflict->assignment.clauseIndex);

        while (!clausesToVisit.empty()) {
            int_t clauseIndex = clausesToVisit.back();
            clausesToVisit.pop_back();

            for (Literal lit : learnedClauses[clauseIndex]) {
                auto it = reasons.find(lit);
                if (it == reasons.end()) {
                    if (theoryFor(lit)->getInfo(lit)->assignedFalse())
                        newClause.push_back(lit);
                } else if (it->second.has_value()) {
                    if (it->second->isDecision())
                        newClause.push_back(lit);
                    else
                        clausesToVisit.push_back(it->second->clauseIndex());
                    it->second.reset();
                }
            }
        }

        std::sort(newClause.begin(), newClause.end());
        auto newEnd = std::unique(newClause.begin(), newClause.end());
        newClause.erase(newEnd, newClause.end());

        //fmt::print("learned: ");
        //dumpClause(newClause);

        conflict.reset();
        addClause(std::move(newClause));
        if (conflict.has_value())
            return learnClause();

        VERIFY(unpropagatedLiterals.size() > 0);
        return true;
    }

    void dumpClause(int_t clauseIndex) {
        dumpClause(learnedClauses[clauseIndex]);
    }
    void dumpClause(const std::vector<Literal>& clause) {
        for (auto lit : clause)
            std::cout << theoryFor(lit)->format(lit) << " ";
        std::cout << '\n';
    }

    // Revert all assignments up to and including level
    void backtrack(int_t targetLevel, std::vector<LiteralAndReason>& revertedLiterals) {
        VERIFY(targetLevel > 0);
        validateMasks();
        std::map<Literal, ClauseOrDecision> firstReasons;
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
                    if (!unpropagatedLiterals.contains(entry.literal)) {
                        for (auto inst : info->instances) {
                            auto& clauseMask = learnedClauseMasks[inst.clauseIndex];
                            auto mask = literalMask(inst.literalIndex);
                            clauseMask |= mask;
                        }
                        unpropagatedLiterals.emplace(entry.literal);
                    }
                    firstReasons.emplace(entry.literal, entry.reason);
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
                    revertedLiterals.push_back({ entry.literal, firstReasons.at(entry.literal) });

                    VERIFY(unpropagatedLiterals.erase(entry.literal) == 1);
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

    void validateMasks() {
        return;
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
            }
        }

        // check linked lists
        auto checkLiteral = [this](Literal lit) {
            const auto* info = theoryFor(lit)->getInfo(lit);
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

        // check decisions
        for (int_t i = 1; i < (int_t)decisions.size(); i++) {
            VERIFY(at(decisions[i]).reason.isDecision());
            VERIFY(at(decisions[i]).reason.decisionLevel() == i);
        }
    }

    void checkAssignment() {
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

private:
    struct Conflict {
        LiteralInstance assignment;
    };

    struct TraceEntry {
        Literal literal;
        ClauseOrDecision reason;
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

    std::set<Literal> unpropagatedLiterals;

    std::vector<TracePosition> decisions;

    std::array<std::unique_ptr<Theory>, Literal::MAX_THEORY_COUNT> theories;

    std::optional<Conflict> conflict;
};

}