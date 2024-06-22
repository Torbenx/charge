#include <check/BooleanVariables.h>
#include <check/SatSolver.h>

namespace check {

ValueTheory::ValueTheory(Solver& solver)
    : m_theoryId(solver.attachTheory(*this)) { }

int_t Solver::attachTheory(ValueTheory& theory) {
    int_t id = valueTheories.size();
    valueTheories.push_back(&theory);
    return id;
}

ReasonTheory::ReasonTheory(Solver& solver, bool propagating)
    : m_theoryId(solver.attachTheory(*this)), m_propagating(propagating) { }

int_t Solver::attachTheory(ReasonTheory& theory) {
    int_t id = reasonTheories.size();
    reasonTheories.push_back(&theory);
    return id;
}

namespace {
    static constexpr int_t EXPLICIT_REASONS_THEORY_ID = 0;
}

Solver::ExplicitReasons::ExplicitReasons(Solver& solver)
    : ReasonTheory(solver, true) { }

Reason Solver::makeClauseReason(int_t clauseIndex, int_t literalIndex) {
    return { .reasonTheory = EXPLICIT_REASONS_THEORY_ID, .data0 = (uint32_t)literalIndex, .data1 = (uint32_t)clauseIndex };
}

bool Solver::ExplicitReasons::testReason(Solver& solver, const Reason& reason) {
    int_t clauseIndex = reason.data1;
    return std::popcount(solver.clauseMasks[clauseIndex]) == 1;
}

ReasonTheory::ClauseAndIndex Solver::ExplicitReasons::reasonToClause(Solver& solver, const Reason& reason) {
    int_t clauseIndex = reason.data1;
    int_t literalIndex = reason.data0;
    return { solver.clauses[clauseIndex], literalIndex };
}

LiteralInstance Solver::ExplicitReasons::asInstance(const Reason& reason) {
    int_t clauseIndex = reason.data1;
    int_t literalIndex = reason.data0;
    return { (uint32_t)literalIndex, (uint32_t)clauseIndex };
}

Solver::Solver()
    : internalVariables(*this), explicitReasons(*this) {
    {
        int_t id = internalVariables.newVariable();
        VERIFY(internalVariables.positiveLiteral(id) == builtins::true_literal);
        VERIFY(internalVariables.negativeLiteral(id) == builtins::false_literal);
        addClause({ builtins::true_literal });
    }
}

std::pair<BooleanValue, BooleanValue> Solver::makeBooleanPair() {
    int_t varId = internalVariables.newVariable();
    return { internalVariables.positiveLiteral(varId), internalVariables.negativeLiteral(varId) };
}

void Solver::addClauseInternal(std::vector<Literal> clause) {
    VERIFY(!clause.empty());
    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE);
    VERIFY(clauses.size() == clauseMasks.size());
    int_t clauseIndex = clauses.size();
    clause_mask_t mask = 0;
    for (int_t index = 0; index < (int_t)clause.size(); index++) {
        LiteralInstance inst { (uint32_t)index, (uint32_t)clauseIndex };
        Literal lit = clause[index];
        infoFor(lit).instances.push_back(inst);
        if (!assignedFalseAndPropagated(lit))
            mask |= literalMask(index);
    }
    VERIFY(mask != 0);
    if (std::popcount(mask) == 1) {
        int_t index = std::countr_zero(mask);
        assignTrue(clause[index], makeClauseReason(clauseIndex, index));
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
    auto& theory = theoryFor(trueLit);
    /*if (reason.isDecision())
        fmt::println("deciding {}", theory.formatValue(*this, trueLit));
    else
        fmt::println("assigning {}", theory.formatValue(*this, trueLit));*/

    Literal falseLit = theory.negate(*this, trueLit);
    auto& info = theory.literalInfo(*this, falseLit);

    TracePosition tracePos(trace.size());
    trace.push_back({ falseLit, reason, info.lastReason, std::nullopt });
    if (info.lastReason.has_value())
        at(*info.lastReason).nextReason = tracePos;
    info.lastReason = tracePos;

    if (!info.firstReason.has_value()) {
        info.firstReason = tracePos;
        queuePropagation(falseLit);
    }

    if (theory.literalInfo(*this, trueLit).assignedFalse()) {
        conflicts.push_back({ trueLit, reason });
        return false;
    }
    return true;
}

bool Solver::propagate() {
    while (firstPropagation.has_value()) {
        Literal literal = firstPropagation.value();
        auto& literalTheory = theoryFor(literal);
        // fmt::println("propagating {}", literalTheory.formatValue(*this, literal));
        removeFirstPropagation();

        literalTheory.propagateFalseAssignment(*this, literal);

        const auto& info = literalTheory.literalInfo(*this, literal);
        // VERIFY(info.assignedFalse());
        // VERIFY(!literalTheory.getInfo(literalTheory.negate(literal)).assignedFalse());

        // This loop up to the first continue is the hottest part of the solver
        for (auto inst : info.instances) {
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
            assignTrue(trueLit, makeClauseReason(inst.clauseIndex, trueLitIndex));
        }
        if (!conflicts.empty())
            return false;
    }
    return true;
}

void Solver::dumpClause(int_t clauseIndex) {
    dumpClause(clauses[clauseIndex]);
}
void Solver::dumpClause(const std::vector<Literal>& clause) {
    for (auto lit : clause)
        std::cout << formatValue(lit) << " ";
    std::cout << '\n';
}

bool Solver::tryLearn(Conflict conflict) {
    VERIFY(conflicts.empty());
    VERIFY(!subTrace.empty());
    SubTraceEntry conflictDecision = subTrace.front();
    VERIFY(conflictDecision.reason.isDecision());
    if (infoFor(conflictDecision.literal).assignedFalse()) {
        // if the decision was not reverted, propagating it will lead to a conflict
        return false;
    }
    tryLearnIndex += 1;

    [[maybe_unused]] auto wasReversed = [&](Literal lit) {
        return std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; }) != subTrace.end();
    };
    [[maybe_unused]] auto wasFalse = [&](Literal lit) { return wasReversed(lit) || infoFor(lit).assignedFalse(); };
    [[maybe_unused]] auto wasTrue = [&](Literal lit) { return wasFalse(negate(lit)); };

    std::vector<Literal> newClause;
    int_t position = subTrace.size();
    int_t openLiterals = 0;
    bool seenSinglePropagatingReason = true;
    std::vector<bool> shouldBeVisited;
    shouldBeVisited.resize(subTrace.size());

    {
        auto [conflictClause, conflictLiteralIndex] = theoryFor(conflict.reason).reasonToClause(*this, conflict.reason);
        for (Literal lit : conflictClause) {
            auto& theory = theoryFor(lit);
            auto& info = theory.literalInfo(*this, lit);
            if (info.assignedFalse()) {
                VERIFY(info.includedInNewClause != tryLearnIndex);
                newClause.push_back(lit);
                info.includedInNewClause = tryLearnIndex;
            } else {
                VERIFY(!shouldBeVisited[info.subTraceIndex]);
                openLiterals += 1;
                shouldBeVisited[info.subTraceIndex] = true;
            }
        }
        if (openLiterals == 0) {
            // All literals in the clause were false, this is a conflict.
            return false;
        }
        if (!theoryFor(conflict.reason).isPropagating())
            seenSinglePropagatingReason = false;
    }

    for (;;) {
        for (;;) {
            position -= 1;
            if (shouldBeVisited[position])
                break;
            if (position == 0) {
                // Entry 0 of the subTrace is always the decision of the reversed level. When we
                // get here we iterated though the entire trace without marking the decision to be
                // visited and thus it is not part of the implication graph that lead to the
                // conflict.
                return false;
            }
        }

        SubTraceEntry entry = subTrace[position];
        openLiterals -= 1;
        if (openLiterals == 0) {
            // Found a UIP
            VERIFY(!infoFor(entry.literal).assignedFalse());

            // Add the new clause but only if it doesn't exists jet
            if (!seenSinglePropagatingReason) {
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
            Literal negatedUIP = negate(entry.literal);
            newClause.push_back(negatedUIP);
            infoFor(negatedUIP).includedInNewClause = tryLearnIndex;
            seenSinglePropagatingReason = true;
        } else
            seenSinglePropagatingReason = false;

        VERIFY(!entry.reason.isDecision());

        auto [clause, forceLiteralIndex] = theoryFor(entry.reason).reasonToClause(*this, entry.reason);
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            Literal lit = clause[index];
            auto& info = infoFor(lit);

            /*if (index != forceLiteralIndex)
                VERIFY(wasFalse(lit));
            else
                VERIFY(wasTrue(lit));*/

            if (info.assignedFalse()) {
                if (info.includedInNewClause != tryLearnIndex) {
                    newClause.push_back(lit);
                    info.includedInNewClause = tryLearnIndex;
                }
            } else if (index != forceLiteralIndex) {
                /*auto it = std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; });
                VERIFY(it != subTrace.end());
                VERIFY(it - subTrace.begin() < position);
                VERIFY(it - subTrace.begin() == (int_t)info.subTraceIndex);*/
                if (!shouldBeVisited[info.subTraceIndex]) {
                    openLiterals += 1;
                    shouldBeVisited[info.subTraceIndex] = true;
                }
            }
        }
        if (!theoryFor(entry.reason).isPropagating())
            seenSinglePropagatingReason = false;
    }
}

bool Solver::analyzeConflicts() {
    VERIFY(!conflicts.empty());

    auto doesConflictPersist = [this](Conflict conflict) {
        bool isReasonValid = theoryFor(conflict.reason).testReason(*this, conflict.reason);
        bool isImpliedLiteralFalse = infoFor(conflict.literal).assignedFalse();
        return isReasonValid && isImpliedLiteralFalse;
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
        auto& theory = theoryFor(entry.literal);
        auto& info = theory.literalInfo(*this, entry.literal);
        // VERIFY(info.firstReason.has_value() && info.lastReason.has_value());

        bool revert = entry.reason.isDecision() || !theoryFor(entry.reason).testReason(*this, entry.reason);
        if (revert) {
            if (info.firstReason.value() == position) {
                // When the first reason is reverted we requeue the propagation.
                info.subTraceIndex = subTrace.size();
                subTrace.push_back({ entry.literal, entry.reason });
                if (entry.nextReason.has_value()) {
                    if (assignedFalseAndPropagated(entry.literal)) {
                        for (auto inst : info.instances) {
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
                        for (auto inst : info.instances) {
                            auto& clauseMask = clauseMasks[inst.clauseIndex];
                            auto mask = literalMask(inst.literalIndex);
                            clauseMask |= mask;
                        }
                    } else {
                        removePropagation(entry.literal);
                    }

                    // revert the literal
                    info.firstReason = std::nullopt;
                    info.lastReason = std::nullopt;
                }
            } else {
                if (entry.nextReason.has_value()) {
                    // tell nextReason to update prevReason
                    at(*entry.nextReason).prevReason = entry.prevReason;
                } else if (entry.prevReason.has_value()) {
                    info.lastReason = entry.prevReason;
                    at(*entry.prevReason).nextReason = std::nullopt;
                } else {
                    // revert the literal
                    info.firstReason = std::nullopt;
                    info.lastReason = std::nullopt;

                    removePropagation(entry.literal);
                }
            }
        } else {
            *(entry.prevReason.has_value() ? &at(*entry.prevReason).nextReason : &info.firstReason) = writePosition;
            *(entry.nextReason.has_value() ? &at(*entry.nextReason).prevReason : &info.lastReason) = writePosition;

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
    auto checkLiteral = [this](Value val) {
        Literal lit = { val };
        const auto& info = infoFor(lit);
        if (!info.assignedFalse())
            return;
        VERIFY(info.firstReason.has_value());
        VERIFY(info.lastReason.has_value());
        TracePosition pos = info.firstReason.value();
        VERIFY(!at(pos).prevReason.has_value());
        VERIFY(at(pos).literal == lit);
        // std::cout << format(lit) << " (" << info.firstReason.index << " .. " << info.lastReason.index << ")" << ": ";
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
        VERIFY(pos == info.lastReason.value());
        // std::cout << '\n';
    };
    for (auto& theory : valueTheories) {
        auto* bTheory = dynamic_cast<BooleanTheory*>(theory);
        if (bTheory != nullptr)
            bTheory->enumerateValues(*this, checkLiteral);
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
            auto& theory = theoryFor(trueLit);
            Literal falseLit = theory.negate(*this, trueLit);
            const auto& info = theory.literalInfo(*this, falseLit);
            VERIFY(info.assignedFalse());

            VERIFY(info.firstReason.has_value());
            VERIFY(info.lastReason.has_value());
            TracePosition pos = info.firstReason.value();
            for (;;) {
                if (at(pos).reason.reasonTheory == EXPLICIT_REASONS_THEORY_ID) {
                    auto inst = explicitReasons.asInstance(at(pos).reason);
                    if ((int_t)inst.clauseIndex == clauseIndex) {
                        VERIFY(mask == literalMask(inst.literalIndex));
                        break;
                    }
                }
                VERIFY(at(pos).nextReason.has_value());
                pos = at(pos).nextReason.value();
            }

            for (Literal lit : clause) {
                if (lit != trueLit) {
                    VERIFY(infoFor(lit).assignedFalse());
                    VERIFY(infoFor(lit).firstReason.value() < pos);
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
        VERIFY(!infoFor(current).prevPropagation.has_value());
        while (infoFor(current).nextPropagation.has_value()) {
            Literal next = infoFor(current).nextPropagation.value();
            VERIFY(infoFor(next).prevPropagation == current);
            current = next;
        }
        VERIFY(current == lastPropagation.value());
    }
}

bool Solver::checkAssignment() {
    for (const auto& clause : clauses) {
        bool foundTrue = false;
        std::optional<Literal> unassignedInternal;
        for (Literal lit : clause) {
            auto& theory = theoryFor(lit);
            if (theory.literalInfo(*this, theory.negate(*this, lit)).assignedFalse()) {
                foundTrue = true;
                break;
            }
            if (lit.theoryId == SOLVER_INTERNAL_VARS_THEORY_ID && !theory.literalInfo(*this, lit).assignedFalse())
                unassignedInternal = lit;
        }
        if (!foundTrue) {
            if (unassignedInternal.has_value()) {
                VERIFY(decideTrue(unassignedInternal.value()));
                return true;
            }
            return false;
        }
    }
    return true;
}

}