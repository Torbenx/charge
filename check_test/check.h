#pragma once

#include <bit>
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

struct LiteralInstance {
    static constexpr int_t LITERAL_BITS = std::bit_width(sizeof(clause_mask_t) * 8 - 1);
    uint32_t literalIndex : LITERAL_BITS;
    uint32_t clauseIndex : 32 - LITERAL_BITS;

    bool operator==(const LiteralInstance&) const = default;
};

struct Theory {
    struct LiteralInfo {
        std::optional<LiteralInstance> propagatedFromClause;
        int_t tracePosition = -1;
        std::vector<LiteralInstance> instances;

        bool assignedFalse() const { return tracePosition != -1; }
        void assignFalse(int_t tracePosition, std::optional<LiteralInstance> clause) {
            this->tracePosition = tracePosition;
            propagatedFromClause = clause;
        }
        void reverseFalseAssignment() {
            tracePosition = -1;
            propagatedFromClause = std::nullopt;
        }
    };

    virtual Literal negate(Literal) = 0;
    virtual const LiteralInfo* getInfo(Literal) = 0;
    virtual void assignFalse(Literal, int_t tracePosition, std::optional<LiteralInstance> clause) = 0;
    virtual void reverseFalseAssignment(Literal) = 0;
    virtual void setTheoryId(int_t id) = 0;
    virtual void addLiteralInstance(Literal, LiteralInstance) = 0;
    virtual std::string format(Literal) = 0;
    virtual ~Theory() = default;
};

struct Solver {
    Solver() { }

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

    bool assignedFalseAndPropagated(Literal lit) {
        int_t tracePos = theoryFor(lit)->getInfo(lit)->tracePosition;
        return tracePos != -1 && tracePos < propagatedPosition;
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
            if (!assignedFalseAndPropagated(lit))
                mask |= literalMask(index);
        }
        VERIFY(mask != 0);
        if (std::popcount(mask) == 1) {
            int_t index = std::countr_zero(mask);
            VERIFY(assignTrue(clause[index], LiteralInstance { (uint32_t)index, (uint32_t)clauseIndex }));
        }
        learnedClauses.emplace_back(std::move(clause));
        learnedClauseMasks.push_back(mask);
        return clauseIndex;
    }

    bool decideTrue(Literal literal) {
        VERIFY(propagatedPosition == (int_t)trace.size());
        return assignTrue(literal, std::nullopt);
    }

    bool assignTrue(Literal trueLit, std::optional<LiteralInstance> clause) {
        Theory* theory = theoryFor(trueLit);
        if (clause.has_value())
            fmt::println("assigning {} from c{}", theory->format(trueLit), (int_t)clause->clauseIndex);
        else
            fmt::println("deciding {}", theory->format(trueLit));

        Literal falseLit = theory->negate(trueLit);
        auto* info = theory->getInfo(falseLit);
        if (!info->assignedFalse()) {
            theory->assignFalse(falseLit, trace.size(), clause);
            trace.push_back(falseLit); // assign 'false' to 'falseLit'
        }

        if (theory->getInfo(trueLit)->assignedFalse())
            return false;
        return true;
    }

    bool propagate() {
        while (propagatedPosition < (int_t)trace.size()) {
            Literal literal = trace[propagatedPosition];
            Theory* literalTheory = theoryFor(literal);
            fmt::println("propagating {}", literalTheory->format(literal));

            validateMasks();
            const auto* info = literalTheory->getInfo(literal);
            VERIFY(info->assignedFalse());
            VERIFY(info->tracePosition == propagatedPosition);

            propagatedPosition += 1;

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

                int_t trueLitIndex = std::countr_zero(clauseMask);
                Literal trueLit = clause[trueLitIndex];
                if (assignTrue(trueLit, LiteralInstance { (uint32_t)trueLitIndex, inst.clauseIndex }))
                    continue;

                if (!conflict.has_value())
                    conflict = Conflict { inst };
            }
            if (conflict.has_value())
                return false;
        }
        validateMasks();
        return true;
    }

    void learnClause() {
        validateMasks();
        VERIFY(conflict.has_value());
        std::set<Literal> seenLiterals;
        std::priority_queue<int_t> literalsToVisit;

        LiteralInstance reason = conflict->assignment;
        for (;;) {
            const auto& clause = learnedClauses[reason.clauseIndex];
            for (int_t index = 0; index < (int_t)clause.size(); index++) {
                Literal lit = clause[index];
                auto* info = theoryFor(lit)->getInfo(lit);
                if (info->assignedFalse()) {
                    if (!seenLiterals.contains(lit)) {
                        seenLiterals.emplace(lit);
                        literalsToVisit.push(info->tracePosition);
                    }
                }
            }

            VERIFY(!literalsToVisit.empty());
            Literal literal = trace[literalsToVisit.top()];
            auto* info = theoryFor(literal)->getInfo(literal);
            VERIFY(info->assignedFalse());
            auto nextReason = info->propagatedFromClause;
            if (!nextReason.has_value()) {
                // decision variable
                break;
            }
            reason = nextReason.value();
            literalsToVisit.pop();
        }
        int_t backtrackPosition = literalsToVisit.top();

        std::vector<Literal> newClause;
        while (!literalsToVisit.empty()) {
            newClause.push_back(trace[literalsToVisit.top()]);
            literalsToVisit.pop();
        }

        {
            Literal conflictDecision = trace[backtrackPosition];
            Theory* theory = theoryFor(conflictDecision);
            fmt::println("conflict from deciding {}", theory->format(theory->negate(conflictDecision)));
            fmt::print("learning ");
            dumpClause(newClause);

            auto* info = theory->getInfo(conflictDecision);
            VERIFY(info->assignedFalse());
            VERIFY(!info->propagatedFromClause.has_value());
            VERIFY(std::find(newClause.begin(), newClause.end(), conflictDecision) != newClause.end());
        }

        backtrack(backtrackPosition);
        addClause(std::move(newClause));
        VERIFY((int_t)trace.size() > propagatedPosition);
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

    // Revert all assignments up to and including position
    void backtrack(int_t position) {
        fmt::println("backtracking to {}", position);
        validateMasks();
        int_t writePosition = position;
        for (; position < propagatedPosition; position++) {
            Literal literal = trace[position];
            Theory* theory = theoryFor(literal);
            auto reason = theory->getInfo(literal)->propagatedFromClause;

            if (reason.has_value()) {
                auto mask = learnedClauseMasks[reason->clauseIndex];
                // fmt::println("c{}: {:#b} == {:#b}", (int_t)reason->clauseIndex, mask, literalMask(reason->literalIndex));
                VERIFY(mask != 0);
                if (std::popcount(mask) == 1)
                    VERIFY(mask == literalMask(reason->literalIndex));
            }

            if (reason.has_value() && learnedClauseMasks[reason->clauseIndex] == literalMask(reason->literalIndex)) {
                theory->assignFalse(literal, writePosition, reason.value()); // update trace position
                trace[writePosition] = literal;
                writePosition += 1;
                continue;
            }
            // fmt::println("reversing {}", theory->format(literal));
            theory->reverseFalseAssignment(literal);

            for (auto inst : theory->getInfo(literal)->instances) {
                auto& clauseMask = learnedClauseMasks[inst.clauseIndex];
                auto mask = literalMask(inst.literalIndex);
                clauseMask |= mask;
            }
        }
        propagatedPosition = writePosition;
        for (; position < (int_t)trace.size(); position++) {
            Literal literal = trace[position];
            Theory* theory = theoryFor(literal);
            auto reason = theory->getInfo(literal)->propagatedFromClause;

            if (reason.has_value()) {
                auto mask = learnedClauseMasks[reason->clauseIndex];
                // fmt::println("c{}: {:#b} == {:#b}", (int_t)reason->clauseIndex, mask, literalMask(reason->literalIndex));
                VERIFY(mask != 0);
                if (std::popcount(mask) == 1)
                    VERIFY(mask == literalMask(reason->literalIndex));
            }

            if (reason.has_value() && learnedClauseMasks[reason->clauseIndex] == literalMask(reason->literalIndex)) {
                theory->assignFalse(literal, writePosition, reason.value()); // update trace position
                trace[writePosition++] = literal;
                continue;
            }
            // fmt::println("reversing {}", theory->format(literal));
            theory->reverseFalseAssignment(literal);
        }
        trace.resize(writePosition);
        fmt::println("backtrack done");
        validateMasks();
    }

    void validateMasks() {
        VERIFY(learnedClauses.size() == learnedClauseMasks.size());
        for (int_t clauseIndex = 0; clauseIndex < (int_t)learnedClauses.size(); clauseIndex++) {
            auto mask = learnedClauseMasks[clauseIndex];
            const auto& clause = learnedClauses[clauseIndex];
            VERIFY((mask & (literalMask(clause.size()) - (clause_mask_t)1)) == mask);
            for (int_t index = 0; index < (int_t)clause.size(); index++) {
                bool bitSet = (mask & literalMask(index)) != 0;
                VERIFY(bitSet == !assignedFalseAndPropagated(clause[index]));
            }
            if (std::popcount(mask) == 1) {
                int_t index = std::countr_zero(mask);
                Literal trueLit = clause[index];
                Theory* theory = theoryFor(trueLit);
                Literal falseLit = theory->negate(trueLit);
                auto* info = theory->getInfo(falseLit);
                VERIFY(info->assignedFalse());
                VERIFY(trace[info->tracePosition] == falseLit);
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
    struct Conflict {
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
    std::vector<Literal> trace;
    int_t propagatedPosition = 0;

    std::array<std::unique_ptr<Theory>, Literal::MAX_THEORY_COUNT> theories;

    std::optional<Conflict> conflict;
};

}