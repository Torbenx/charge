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

void ClauseBuilder::add(Solver& solver, BooleanValue val) {
    solver.impl().sat.addToClause(*this, val);
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
        println("deciding {}", formatValue(trueLit));
    } else {
        print("assigning {}, reason: ", formatValue(trueLit));
        dumpClause(theoryFor(reason).reasonToClause(*this, reason).clause);
    }*/

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
    VERIFY(conflicts.empty());

    while (firstPropagation.has_value()) {
        Literal literal = firstPropagation.value();
        // println("propagating {}", literalTheory.formatValue(*this, literal));
        removeFirstPropagation();
        interface().propagateAssignment(literal);
        if (!conflicts.empty())
            return false;
    }
    return true;
}

bool SatCore::tryLearn(Conflict conflict) {
    VERIFY(conflicts.empty());
    VERIFY(!subTrace.empty());
    SubTraceEntry conflictDecision = subTrace.front();
    VERIFY(conflictDecision.reason.isDecision());
    if (infoFor(conflictDecision.literal).tentativelyTrue()) {
        // If the decision was not reverted, propagating it will lead to a conflict
        return false;
    }

    [[maybe_unused]] auto wasReversed = [&](Literal lit) {
        return std::find_if(subTrace.begin(), subTrace.end(), [lit](SubTraceEntry entry) { return entry.literal == lit; }) != subTrace.end();
    };
    [[maybe_unused]] auto wasTrue = [&](Literal lit) { return wasReversed(lit) || infoFor(lit).tentativelyTrue(); };
    [[maybe_unused]] auto wasFalse = [&](Literal lit) { return wasTrue(!lit); };

    auto newClause = beginClause();
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
                addToClause(newClause, falseLit);
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
        if (!isFullyPropagating(conflict.reason.kind()))
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
            VERIFY(!infoFor(entry.literal).tentativelyTrue());

            // Add the new clause but only if it doesn't exists jet
            if (!seenSinglePropagatingReason) {
                addToClause(newClause, !entry.literal);
                // print("learning: "); dumpClause(newClause);
                auto span = endClause(newClause);
                interface().learnClause({ span.begin(), span.end() });
                VERIFY(conflicts.empty());
            }

            if (entry.reason.isDecision()) {
                VERIFY(firstPropagation.has_value());
                return true;
            }

            newClause = beginClause();
            addToClause(newClause, entry.literal);
            seenSinglePropagatingReason = true;
        } else
            seenSinglePropagatingReason = false;

        VERIFY(!entry.reason.isDecision());

        auto [clause, forceLiteralIndex] = interface().reasonToClause(entry.literal, entry.reason);
        for (int_t index = 0; index < (int_t)clause.size(); index++) {
            Literal lit = clause[index];
            auto& info = infoFor(!lit);

            if (index != forceLiteralIndex)
                VERIFY(wasFalse(lit));
            else
                VERIFY(wasTrue(lit));

            if (info.tentativelyTrue()) {
                addToClause(newClause, lit);
            } else if (index != forceLiteralIndex) {
                auto it = std::find_if(subTrace.begin(), subTrace.end(), [l = !lit](SubTraceEntry entry) { return entry.literal == l; });
                VERIFY(it != subTrace.end());
                VERIFY(it - subTrace.begin() < position);
                VERIFY(it - subTrace.begin() == (int_t)info.subTraceIndex);
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
            if (currentDecisionLevel() == -1)
                return false;

            backtrack(currentDecisionLevel());
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

void SatCore::backtrack(int_t targetLevel) {
    VERIFY(targetLevel >= 0);
    TracePosition position = decisions[targetLevel];
    while ((int_t)decisions.size() > targetLevel)
        decisions.pop_back();
    VERIFY(currentDecisionLevel() == targetLevel - 1);

    interface().onBacktrack();

    subTrace.clear();
    subTrace.reserve(trace.size() - position.index);

    TracePosition writePosition = position;
    TracePosition traceEnd = TracePosition(trace.size());
    for (; position < traceEnd; position++) {
        const TraceEntry entry = at(position);
        auto& info = infoFor(entry.literal);
        VERIFY(info.firstReason.has_value() && info.lastReason.has_value());

        bool revert = !interface().testReason(entry.literal, entry.reason);
        if (revert) {
            if (info.firstReason.value() == position) {
                // When the first reason is reverted we requeue the propagation.
                info.subTraceIndex = subTrace.size();
                subTrace.push_back({ entry.literal, entry.reason });
                if (assignedTrue(entry.literal)) {
                    interface().unapplyAssignment(entry.literal);
                    queuePropagation(entry.literal);
                }
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
            if (info.firstReason.value() == position) {
                if (assignedTrue(entry.literal)) {
                    interface().reapplyAssignment(entry.literal);
                }
            }
            *(entry.prevReason.has_value() ? &at(*entry.prevReason).nextReason : &info.firstReason) = writePosition;
            *(entry.nextReason.has_value() ? &at(*entry.nextReason).prevReason : &info.lastReason) = writePosition;

            at(writePosition) = entry;
            writePosition += 1;
        }
    }
    trace.erase(trace.begin() + writePosition.index, trace.end());

    // checkInvariances();
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
        // print("{} ({} .. {}): {}", formatValue(lit), info.firstReason->index, info.lastReason->index, pos.index);

        while (at(pos).nextReason.has_value()) {
            TracePosition newPos = at(pos).nextReason.value();
            VERIFY(newPos > pos);
            // print(" -> {}", newPos.index);
            VERIFY(at(newPos).literal == lit);
            VERIFY(at(newPos).prevReason.has_value());
            VERIFY(at(newPos).prevReason.value() == pos);
            pos = newPos;
        }
        VERIFY(pos == info.lastReason.value());
        // println("");
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