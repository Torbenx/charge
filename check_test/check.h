#pragma once

#include <bit>
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

struct LiteralInstance {
    static constexpr int_t LITERAL_BITS = std::bit_width(sizeof(clause_mask_t) * 8 - 1);
    uint32_t literalIndex : LITERAL_BITS;
    uint32_t clauseIndex : 32 - LITERAL_BITS;
};

struct Theory {
    struct LiteralInfo {
        int_t level = -1;
        std::optional<LiteralInstance> propagatedFromClause;
        bool propagated = false;
        std::vector<LiteralInstance> instances;

        bool assignedFalse() const { return level != -1; }
        bool assignedFalseAndPropagated() const { return propagated; }
        void markPropagated() { propagated = true; }
        void assignFalse(int_t level, std::optional<LiteralInstance> clause) {
            this->level = level;
            propagatedFromClause = clause;
        }
        void reverseFalseAssignment() {
            level = -1;
            propagatedFromClause = std::nullopt;
            propagated = false;
        }
    };

    virtual Literal negate(Literal) = 0;
    virtual const LiteralInfo* getInfo(Literal) = 0;
    virtual void assignFalse(Literal, int_t level, std::optional<LiteralInstance> clause) = 0;
    virtual void markPropagated(Literal) = 0;
    virtual void reverseFalseAssignment(Literal) = 0;
    virtual void setTheoryId(int_t id) = 0;
    virtual void addLiteralInstance(Literal, LiteralInstance) = 0;
    virtual std::string format(Literal) = 0;
    virtual ~Theory() = default;
};

struct Solver {
    Solver() {
        increaseLevel();
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

    int_t currentLevel() {
        return levelTraces.size() - 1;
    }

    void increaseLevel() {
        VERIFY(assignQueue.empty());
        levelTraces.emplace_back();
    }

    int_t assignmentLevel(const std::vector<Literal>& clause, int_t baseLevel) {
        int_t assignLevel = baseLevel;
        for (Literal other : clause) {
            int_t otherLevel = theoryFor(other)->getInfo(other)->level;
            assignLevel = std::max(assignLevel, otherLevel);
        }
        return assignLevel;
    }

    int_t addClause(std::vector<Literal> clause) {
        VERIFY(!clause.empty());
        VERIFY(clause.size() <= sizeof(clause_mask_t) * 8);
        VERIFY(learnedClauses.size() == learnedClauseMasks.size());
        int_t clauseIndex = learnedClauses.size();
        clause_mask_t mask = 0;
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            LiteralInstance inst { (uint32_t)index, (uint32_t)clauseIndex };
            Literal lit = clause[index];
            Theory* theory = theoryFor(lit);
            theory->addLiteralInstance(lit, inst);
            if (!theory->getInfo(lit)->assignedFalseAndPropagated())
                mask |= literalMask(index);
        }
        VERIFY(mask != 0);
        if (std::popcount(mask) == 1) {
            int_t index = std::countr_zero(mask);
            VERIFY(assignTrue(clause[index], assignmentLevel(clause, 0), LiteralInstance { (uint32_t)index, (uint32_t)clauseIndex }));
        }
        learnedClauses.emplace_back(std::move(clause));
        learnedClauseMasks.push_back(mask);
        return clauseIndex;
    }

    bool decideTrue(Literal literal) {
        increaseLevel();
        return assignTrue(literal, currentLevel(), std::nullopt);
    }

    bool assignTrue(Literal trueLit, int_t level, std::optional<LiteralInstance> clause) {
        Theory* theory = theoryFor(trueLit);
        if (clause.has_value())
            fmt::println("assigning {} at level {} from c{}", theory->format(trueLit), level, (int_t)clause->clauseIndex);
        else
            fmt::println("deciding {} at level {}", theory->format(trueLit), level);

        Literal falseLit = theory->negate(trueLit);
        auto* info = theory->getInfo(falseLit);
        if (!info->assignedFalse()) {
            theory->assignFalse(falseLit, level, clause);
            assignQueue.push_back({ (uint32_t)level, falseLit }); // assign 'false' to 'falseLit'
            levelTraces[level].push_back(falseLit);
        } else {
            VERIFY(info->level <= level);
        }

        return !theory->getInfo(trueLit)->assignedFalse();
    }

    bool propagate() {
        while (assignQueuePosition < (int_t)assignQueue.size()) {
            auto [literalLevel, literal] = assignQueue[assignQueuePosition++];
            Theory* literalTheory = theoryFor(literal);
            fmt::println("propagating {}", literalTheory->format(literal));
            literalTheory->markPropagated(literal);
            const auto* info = literalTheory->getInfo(literal);
            VERIFY(info->assignedFalse());

            for (auto inst : info->instances) {
                clause_mask_t& clauseMask = learnedClauseMasks[inst.clauseIndex];
                fmt::println("c{}: {:#b}", (int_t)inst.clauseIndex, clauseMask);
                int popcnt = std::popcount(clauseMask);
                VERIFY((clauseMask & literalMask(inst.literalIndex)) != (clause_mask_t)0);
                clauseMask &= ~literalMask(inst.literalIndex);
                if (popcnt > 2)
                    continue;

                VERIFY(popcnt == 2);

                // Unit clause propagation:
                // All other literals in this clause are 'false' thus the last one must be 'true'.
                const auto& clause = learnedClauses[inst.clauseIndex];
                int_t assignLevel = assignmentLevel(clause, literalLevel);

                Literal trueLit = clause[std::countr_zero(clauseMask)];
                if (assignTrue(trueLit, assignLevel, inst))
                    continue;

                if (!conflict.has_value() || assignLevel < (int_t)conflict->level)
                    conflict = Conflict { (uint32_t)assignLevel, inst };
            }
            if (conflict.has_value())
                return false;
        }
        assignQueue.clear();
        assignQueuePosition = 0;
        validateMasks();
        return true;
    }

    void learnClause() {
        VERIFY(conflict.has_value());
        std::vector<Literal> newClause;
        std::vector<LiteralInstance> clausesToVisit;
        clausesToVisit.push_back(conflict.value().assignment);
        int_t conflictLevel = conflict.value().level;
        std::vector<bool> seenClauses(learnedClauses.size());

        while (!clausesToVisit.empty()) {
            int_t clauseIndex = clausesToVisit.back().clauseIndex;
            const auto& clause = learnedClauses[clauseIndex];
            clausesToVisit.pop_back();
            for (int_t index = 0; index < (int_t)clause.size(); index++) {
                Literal lit = clause[index];
                auto* info = theoryFor(lit)->getInfo(lit);
                if (!info->assignedFalse())
                    continue;
                auto reason = info->propagatedFromClause;
                if (!reason.has_value() || info->level < conflictLevel) {
                    newClause.push_back(lit);
                } else {
                    VERIFY(info->level == conflictLevel);
                    if (!seenClauses[reason->clauseIndex]) {
                        seenClauses[reason->clauseIndex] = true;
                        clausesToVisit.push_back(reason.value());
                    }
                }
            }
        }
        std::sort(newClause.begin(), newClause.end());
        auto newEnd = std::unique(newClause.begin(), newClause.end());
        newClause.erase(newEnd, newClause.end());

        {
            Literal conflictDecision = levelTraces[conflictLevel].front();
            fmt::println("conflict at level {} from deciding {}", conflictLevel, theoryFor(conflictDecision)->format(conflictDecision));
            fmt::print("learning ");
            dumpClause(newClause);

            auto* info = theoryFor(conflictDecision)->getInfo(conflictDecision);
            VERIFY(info->assignedFalse());
            VERIFY(!info->propagatedFromClause.has_value());
            VERIFY(std::find(newClause.begin(), newClause.end(), conflictDecision) != newClause.end());
        }

        backtrack(conflictLevel - 1);
        addClause(std::move(newClause));
        VERIFY((int_t)assignQueue.size() > assignQueuePosition);
        conflict.reset();
    }

    void dumpClause(int_t clauseIndex) {
        dumpClause(learnedClauses[clauseIndex]);
    }
    void dumpClause(const std::vector<Literal>& clause) {
        for (auto lit : clause)
            std::cout << theoryFor(lit)->format(lit) << " ";
        std::cout << '\n';
    }

    // Revert all levels up to and including level
    void backtrack(int_t targetLevel) {
        auto predicate = [targetLevel](PendingAssignment assign) {
            return (int_t)assign.level > targetLevel;
        };
        auto newEnd = std::remove_if(assignQueue.begin() + assignQueuePosition, assignQueue.end(), predicate);
        assignQueue.erase(newEnd, assignQueue.end());

        for (int_t level = currentLevel(); level > targetLevel; level--) {
            auto& trace = levelTraces[level];
            while (!trace.empty()) {
                Literal literal = trace.back();
                trace.pop_back();
                Theory* theory = theoryFor(literal);
                fmt::println("reversing {}", theory->format(literal));
                theory->reverseFalseAssignment(literal);

                for (auto inst : theory->getInfo(literal)->instances) {
                    auto& clauseMask = learnedClauseMasks[inst.clauseIndex];
                    auto mask = literalMask(inst.literalIndex);
                    clauseMask |= mask;
                }
            }
        }
        levelTraces.resize(targetLevel + 1);
    }

    void validateMasks() {
        VERIFY(learnedClauses.size() == learnedClauseMasks.size());
        for (int_t clauseIndex = 0; clauseIndex < (int_t)learnedClauses.size(); clauseIndex++) {
            auto mask = learnedClauseMasks[clauseIndex];
            const auto& clause = learnedClauses[clauseIndex];
            VERIFY((mask & (literalMask(clause.size()) - (clause_mask_t)1)) == mask);
            for (int_t index = 0; index < (int_t)clause.size(); index++) {
                bool bitSet = (mask & literalMask(index)) != 0;
                bool assignedFalse = theoryFor(clause[index])->getInfo(clause[index])->assignedFalseAndPropagated();
                VERIFY(bitSet == !assignedFalse);
            }
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
    struct PendingAssignment {
        uint32_t level;
        Literal literal;
    };

    struct Conflict {
        uint32_t level;
        LiteralInstance assignment;
    };

    static clause_mask_t literalMask(int_t index) {
        return (clause_mask_t)1 << index;
    }

    // Bitmasks for learned clauses. Contains 1 for literals that are not assigned 'false'.
    std::vector<clause_mask_t> learnedClauseMasks;

    // The actual learned clauses
    std::vector<std::vector<Literal>> learnedClauses;

    // Queue of literals that were or should be assigned 'false'
    std::vector<std::vector<Literal>> levelTraces;
    std::vector<PendingAssignment> assignQueue;
    int_t assignQueuePosition = 0;

    std::array<std::unique_ptr<Theory>, Literal::MAX_THEORY_COUNT> theories;

    std::optional<Conflict> conflict;
};

}