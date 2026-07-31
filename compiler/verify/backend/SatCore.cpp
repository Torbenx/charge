#include <verify/backend/SatCore.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

SatCore::Interface& SatCore::interface() {
    return *ReverseMemberPointer<&SolverImpl::sat>::reverse(this);
}

SatCore::LiteralInfo& SatCore::Interface::infoFor(Literal lit) {
    auto& impl = static_cast<SolverImpl&>(*this);
    return impl.literalInfos[lit];
}

bool ClauseBuilder::add(Solver& solver, Bool val) {
    return solver.impl().sat.addToClause(*this, val);
}

bool SatCore::addToLearnClause(Literal lit) {
    if (infoFor(lit).lastContainingLearnClauseId != learnClauseId) {
        learnClause.push_back(lit);
        infoFor(lit).lastContainingLearnClauseId = learnClauseId;
        return true;
    }
    return false;
}

void SatCore::queuePropagation(Literal lit) {
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
void SatCore::removeFirstPropagation() {
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
void SatCore::removePropagation(Literal lit) {
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

bool SatCore::assignedTrue(Literal lit) {
    const auto& info = infoFor(lit);
    if (!info.tentativelyTrue())
        return false;
    return lit != firstPropagation && !info.prevPropagation.has_value();
}

Reason SatCore::firstReason(Literal lit) {
    const auto& info = infoFor(lit);
    VERIFY(info.firstReason.has_value());
    return at(*info.firstReason).reason;
}

ClauseAndIndex SatCore::justifyAssignment(Literal lit) {
    return interface().reasonToClause(lit, firstReason(lit));
}

bool SatCore::alwaysTrue(Literal lit) {
    if (!assignedTrue(lit))
        return false;

    const auto& info = infoFor(lit);
    VERIFY(info.lastReason.has_value());
    const Reason& lastReason = at(info.lastReason.value()).reason;
    return lastReason.kind() == ReasonKind::Always;
}

void SatCore::decideTrue(Literal literal) {
    VERIFY(!firstPropagation.has_value());
    VERIFY(!infoFor(!literal).tentativelyTrue());
    decisions.push_back(TracePosition(trace.size()));
    assignTrue(literal, makeReason<ReasonKind::Decision>({ .decisionLevel = (uint32_t)currentDecisionLevel() }));
    interface().onNewDecisionLevel();
}

void SatCore::assignTrue(Literal trueLit, const Reason& reason) {
    /*if (reason.isDecision()) {
        dbgln("deciding {}", formatValue(trueLit));
    } else {
        dbgprint("assigning {}, reason: ", formatValue(trueLit));
        dumpClause(theoryFor(reason).reasonToClause(*this, reason).clause);
    }*/
    VERIFY(!backtracking);

    auto& info = infoFor(trueLit);
    TracePosition tracePos(trace.size());
    if (info.lastReason.has_value()) {
        auto& entry = at(*info.lastReason);
        if (entry.reason.kind() == ReasonKind::Always)
            return;
        entry.nextReason = tracePos;
    }
    trace.push_back({ trueLit, reason, info.lastReason, std::nullopt });
    info.lastReason = tracePos;

    if (!info.firstReason.has_value()) {
        info.firstReason = tracePos;
        queuePropagation(trueLit);
    }

    if (infoFor(!trueLit).tentativelyTrue()) {
        conflicts.push_back({ trueLit, reason });
    }
}

bool SatCore::propagate() {
    VERIFY(!backtracking);

    if (!conflicts.empty())
        return false;

    while (firstPropagation.has_value()) {
        Literal literal = firstPropagation.value();
        // dbgln("propagating {}", literalTheory.formatValue(*this, literal));
        removeFirstPropagation();
        interface().propagateAssignment(literal);
        if (!conflicts.empty())
            return false;
    }
    return true;
}

std::pair<std::vector<std::vector<Bool>>, bool> SatCore::tryLearn(Conflict conflict) {
    VERIFY(conflicts.empty());
    VERIFY(!subTrace.empty());
    SubTraceEntry conflictDecision = subTrace.front();
    VERIFY(conflictDecision.reason.isDecision());
    if (infoFor(conflictDecision.literal).tentativelyTrue()) {
        // If the decision was not reverted, propagating it will lead to a conflict
        return {};
    }

    [[maybe_unused]] auto wasReversed = [&](Literal lit) {
        return std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; }) != subTrace.end();
    };
    [[maybe_unused]] auto wasTrue = [&](Literal lit) { return wasReversed(lit) || infoFor(lit).tentativelyTrue(); };
    [[maybe_unused]] auto wasFalse = [&](Literal lit) { return wasTrue(!lit); };

    learnClauseId += 1;
    learnClause.clear();
    int_t position = subTrace.size();
    int_t openLiterals = 0;
    bool seenSinglePropagatingReason = true;
    std::vector<bool> shouldBeVisited;
    shouldBeVisited.resize(subTrace.size());

    {
        auto [conflictClause, conflictLiteralIndex] = interface().reasonToClause(conflict.literal, conflict.reason);
        for (Literal falseLit : conflictClause) {
            Literal trueLit = !falseLit;
            auto& info = infoFor(trueLit);

            VERIFY(wasTrue(trueLit));

            if (info.tentativelyTrue()) {
                addToLearnClause(falseLit);
            } else {
                VERIFY(!shouldBeVisited[info.subTraceIndex]);
                openLiterals += 1;
                shouldBeVisited[info.subTraceIndex] = true;
            }
        }
        if (openLiterals == 0) {
            // All literals in the clause were false, this is a conflict.
            return {};
        }
        if (!isFullyPropagating(conflict.reason.kind()))
            seenSinglePropagatingReason = false;
    }

    std::vector<std::vector<Literal>> learnedClauses;
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
                return { std::move(learnedClauses), false };
            }
        }

        SubTraceEntry entry = subTrace[position];
        openLiterals -= 1;
        if (openLiterals == 0) {
            // Found a UIP
            VERIFY(!infoFor(entry.literal).tentativelyTrue());

            // Add the new clause but only if it doesn't exists jet
            if (!seenSinglePropagatingReason) {
                addToLearnClause(!entry.literal);
                // dbgprint("learning: "); dumpClause(learnClause);
                learnedClauses.push_back(learnClause);
            }

            if (entry.reason.isDecision())
                return { std::move(learnedClauses), true };

            learnClauseId += 1;
            learnClause.clear();
            addToLearnClause(entry.literal);
            seenSinglePropagatingReason = true;
        } else
            seenSinglePropagatingReason = false;

        VERIFY(!entry.reason.isDecision());

        auto [clause, forceLiteralIndex] = interface().reasonToClause(entry.literal, entry.reason);
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            Literal lit = clause[index];
            auto& info = infoFor(!lit);

            // if (index != forceLiteralIndex)
            //     VERIFY(wasFalse(lit));
            // else
            //     VERIFY(wasTrue(lit));

            if (info.tentativelyTrue()) {
                addToLearnClause(lit);
            } else if (index != forceLiteralIndex) {
                // auto it = std::find_if(subTrace.begin(), subTrace.end(), [l = !lit](SubTraceEntry entry) { return entry.literal == l; });
                // VERIFY(it != subTrace.end());
                // VERIFY(it - subTrace.begin() < position);
                // VERIFY(it - subTrace.begin() == (int_t)info.subTraceIndex);
                if (!shouldBeVisited[info.subTraceIndex]) {
                    openLiterals += 1;
                    shouldBeVisited[info.subTraceIndex] = true;
                }
            }
        }
        if (!isFullyPropagating(entry.reason.kind()))
            seenSinglePropagatingReason = false;
    }
}

bool SatCore::analyzeConflicts() {
    VERIFY(!conflicts.empty());

    auto doesConflictPersist = [this](Conflict conflict) {
        bool isReasonValid = interface().testReason(conflict.literal, conflict.reason);
        bool isImpliedLiteralFalse = infoFor(!conflict.literal).tentativelyTrue();
        return isReasonValid && isImpliedLiteralFalse;
    };

    for (;;) {
        Conflict drivingConflict = conflicts.front();

        // Backtrack until all conflicts are resolved
        for (;;) {
            if (currentDecisionLevel() == -1)
                return false;

            beginBacktrack(currentDecisionLevel());

            // Note: We have to remove all conflicts that no longer hold. Their reasons may contain
            //       handles to data that is removed by endBacktrack(). As an invariance we only
            //       require that reasons remain valid until the endBacktrack() that matches the
            //       beginBacktrack() call where they stopped being forcing.
            std::erase_if(conflicts, [&](const Conflict& c) { return !doesConflictPersist(c); });
            if (conflicts.empty())
                break;
            drivingConflict = conflicts.front();

            endBacktrack();
        }

        // Learn from the last conflict that was resolved
        auto [learnedClauses, result] = tryLearn(drivingConflict);
        endBacktrack();
        for (auto& clause : learnedClauses) {
            interface().learnClause(std::move(clause));
        }
        if (result) {
            VERIFY(firstPropagation.has_value());
            return true;
        }

        // tryLearn() detected that a conflict still persists, find it by propagating
        propagate();
        VERIFY(!conflicts.empty());
    }
}

void SatCore::beginBacktrack(int_t targetLevel) {
    VERIFY(!backtracking);
    VERIFY(targetLevel >= 0);
    VERIFY(targetLevel <= currentDecisionLevel());
    backtracking = true;
    TracePosition position = decisions[targetLevel];
    decisions.erase(decisions.begin() + targetLevel, decisions.end());
    VERIFY(currentDecisionLevel() == targetLevel - 1);

    interface().onBeginBacktrack();

    subTrace.clear();
    subTrace.reserve(trace.size() - position.index);

    TracePosition writePosition = position;
    TracePosition traceEnd = TracePosition(trace.size());
    for (; position < traceEnd; position++) {
        const TraceEntry entry = at(position);
        auto& info = infoFor(entry.literal);
        VERIFY(info.firstReason.has_value() && info.lastReason.has_value());

        if (info.firstReason.value() == position) {
            // When the first reason is reverted we requeue the propagation.
            if (firstPropagation != entry.literal && !info.prevPropagation.has_value()) {
                interface().unapplyAssignment(entry.literal);
                queuePropagation(entry.literal);
            }
        }

        bool revert = !interface().testReason(entry.literal, entry.reason);
        if (revert) {
            if (info.firstReason.value() == position) {
                info.subTraceIndex = subTrace.size();
                subTrace.push_back({ entry.literal, entry.reason });
            }
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
        } else {
            *(entry.prevReason.has_value() ? &at(*entry.prevReason).nextReason : &info.firstReason) = writePosition;
            *(entry.nextReason.has_value() ? &at(*entry.nextReason).prevReason : &info.lastReason) = writePosition;

            at(writePosition) = entry;
            writePosition += 1;
        }
    }
    trace.erase(trace.begin() + writePosition.index, trace.end());

    // checkInvariances();
}

void SatCore::endBacktrack() {
    VERIFY(backtracking);
    backtracking = false;
    interface().onEndBacktrack();
}

std::vector<Reason> SatCore::collectReasons(Literal trueLit) {
    const auto& info = infoFor(trueLit);
    VERIFY(info.tentativelyTrue());

    VERIFY(info.firstReason.has_value());
    VERIFY(info.lastReason.has_value());
    std::vector<Reason> result;
    TracePosition pos = info.firstReason.value();
    for (;;) {
        result.push_back(at(pos).reason);
        if (!at(pos).nextReason.has_value())
            break;
        pos = at(pos).nextReason.value();
    }
    return result;
}

/*void SatCore::checkInvariances() {
    // check reason linked lists
    auto checkLiteral = [this](Value val) {
        Literal lit = { val };
        const auto& info = infoFor(lit);
        if (!info.tentativelyTrue())
            return;
        VERIFY(info.firstReason.has_value());
        VERIFY(info.lastReason.has_value());
        TracePosition pos = info.firstReason.value();
        VERIFY(!at(pos).prevReason.has_value());
        VERIFY(at(pos).literal == lit);
        // dbgprint("{} ({} .. {}): {}", formatValue(lit), info.firstReason->index, info.lastReason->index, pos.index);

        while (at(pos).nextReason.has_value()) {
            TracePosition newPos = at(pos).nextReason.value();
            VERIFY(newPos > pos);
            // dbgprint(" -> {}", newPos.index);
            VERIFY(at(newPos).literal == lit);
            VERIFY(at(newPos).prevReason.has_value());
            VERIFY(at(newPos).prevReason.value() == pos);
            pos = newPos;
        }
        VERIFY(pos == info.lastReason.value());
        // dbgln("");
    };
    for (auto& theory : valueTheories) {
        auto* bTheory = dynamic_cast<BooleanTheory*>(theory.theory);
        if (bTheory != nullptr) {
            for (int_t valueId = 0; valueId < valueCount(*bTheory); valueId++) {
                checkLiteral(Value { (uint32_t)bTheory->theoryId(), (uint32_t)valueId });
            }
        }
    }

    // check decisions
    for (int_t level = 0; level < (int_t)decisions.size(); level++) {
        VERIFY(at(decisions[level]).reason.isDecision());
        VERIFY(at(decisions[level]).reason.decisionLevel() == level);
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
}*/

}