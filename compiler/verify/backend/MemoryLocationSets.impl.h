#pragma once

#include <verify/backend/MemoryLocationSets.h>
#include <verify/backend/PrefixIndex.impl.h>
#include <verify/backend/SolverImpl.h>

#include <algorithm>

namespace verify::backend {

template<typename Derived, typename PrefixImpl>
MemoryLocationSets<Derived, PrefixImpl>::MemoryLocationSets(Solver&) { }

template<typename Derived, typename PrefixImpl>
Sets& MemoryLocationSets<Derived, PrefixImpl>::baseTheory(Solver& solver) {
    return solver.impl().setTheory(params().setSort);
}

template<typename Derived, typename PrefixImpl>
constexpr MemoryLocationSetsParams MemoryLocationSets<Derived, PrefixImpl>::params() {
    return Derived::PARAMS;
}

template<typename Derived, typename PrefixImpl>
Bool MemoryLocationSets<Derived, PrefixImpl>::containmentOf(Solver& solver, PrefixIndexWordId word) {
    return baseTheory(solver).mapToBool(solver, prefixes.elementOf(word), prefixes.containmentOf(word));
}

template<typename Derived, typename PrefixImpl>
bool MemoryLocationSets<Derived, PrefixImpl>::joinedRepresentative(Solver& solver, const ElementState& state, MemoryDeclaration declaration) {
    if (!state.representative.has_value())
        return false;
    MemoryDeclaration representative = locationOf(state.representative.value()).declaration;
    return solver.assignedEqual(representative, declaration);
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::addWord(Solver& solver, ElementId element, Containment cont) {
    // A set holds everything the sets of its members hold, so a containment is a path and an
    // exclusion is a prefix candidate
    PrefixRole role = cont.contained() ? PrefixRole::Path : PrefixRole::Candidate;
    prefixes.addWord(solver, derived().toWord(setHandle(cont.set())), element, cont, role);
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::addPending(Solver& solver, ElementId element, Containment cont) {
    TracePosition position = pending.push({ .element = element, .containment = cont });
    stateOf(element).pendingPositions.push_back(position);
    solver.addUse(locationOf(cont.set()).declaration, Use(params().pendingRewriteUse, position.index));
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::promotePending(Solver& solver, const ElementState& state, TracePosition position) {
    PendingContainment& entry = pending[position];
    if (entry.promoted)
        return;
    if (!joinedRepresentative(solver, state, locationOf(entry.containment.set()).declaration))
        return;

    // The link that joined the declarations is of the current decision level, so the word registered
    // here is unregistered again by the same backtrack that reverts the promotion.
    entry.promoted = true;
    promotionTrace.push(position);
    addWord(solver, entry.element, entry.containment);
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::promotePendingOf(Solver& solver, ElementId element) {
    const ElementState& state = stateOf(element);
    for (TracePosition position : state.pendingPositions)
        promotePending(solver, state, position);
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::propagateContainment(Solver& solver, ElementId element, Sets::Containment containment) {
    Set set = containment.set();

    if (element == baseTheory(solver).forAllElement())
        return;

    MemoryLocation location = locationOf(set);
    ElementState& state = stateOf(element);

    bool newRepresentative = false;
    if (containment.contained()) {
        if (!state.representative.has_value()) {
            VERIFY(solver.currentDecisionLevel() >= 0); // No positive set assignments are made without a decision
            state.representative = set;
            representativeTrace.push(element);
            newRepresentative = true;
            solver.addUse(location.declaration, Use(params().representativeRewriteUse, element.id()));
        } else {
            // Distinct declarations describe distinct memory, so an element of both locations means
            // that the declarations are the same
            Set representative = state.representative.value();
            solver.assignTrue(solver.equality(locationOf(representative).declaration, location.declaration),
                makeReason<params().declarationsShareElementReason>({ element, representative, set }));
        }
    }

    if (joinedRepresentative(solver, state, location.declaration))
        addWord(solver, element, containment);
    else
        addPending(solver, element, containment);

    // The pending containments were all waiting for a representative to compare against
    if (newRepresentative)
        promotePendingOf(solver, element);
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::propagateRewrite(Solver& solver, Use use) {
    switch (use.kind()) {
    case params().pendingRewriteUse: {
        TracePosition position { use.id() };
        promotePending(solver, stateOf(pending[position].element), position);
        break;
    }
    case params().representativeRewriteUse:
        promotePendingOf(solver, ElementId(use.id()));
        break;
    case PrefixImpl::wordUse:
        prefixes.propagateRewrite(solver, use);
        break;
    default:
        VERIFY_NOT_REACHED();
    }
}

template<typename Derived, typename PrefixImpl>
bool MemoryLocationSets<Derived, PrefixImpl>::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == params().declarationsShareElementReason) {
        auto data = reason.get(params().declarationsShareElementReason);
        auto [setA, setB] = data.sets();
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setA))
            && baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setB));
    }

    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get(PrefixImpl::hitReason);
    // The locations are only comparable as long as they belong to the same declaration
    return solver.assignedEqual(declarationOf(data.prefix), declarationOf(data.path))
        && prefixes.isPrefixOf(data.prefix, data.path)
        && solver.assignedTrue(containmentOf(solver, data.prefix))
        && solver.assignedTrue(containmentOf(solver, data.path));
}

template<typename Derived, typename PrefixImpl>
ClauseAndIndex MemoryLocationSets<Derived, PrefixImpl>::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == params().declarationsShareElementReason) {
        auto data = reason.get(params().declarationsShareElementReason);
        auto [setA, setB] = data.sets();

        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setA)));
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setB)));
        return { solver.viewClause(clause), 0 };
    }

    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get(PrefixImpl::hitReason);

    ClauseBuilder clause = solver.beginClause();
    clause.add(solver, assignedLiteral);
    // The member of the prefix is a prefix of the member of the path, which makes the set of the
    // path a subset of the set of the prefix. So the element cannot be excluded from the one and
    // contained in the other at the same time.
    clause.add(solver, !containmentOf(solver, data.prefix));
    clause.add(solver, !containmentOf(solver, data.path));
    prefixes.explainPrefix(solver, data.prefix, data.path, clause);
    // The subset relation only holds within one declaration
    solver.impl().memoryDeclarationEquality.explainEqual(solver, declarationOf(data.prefix), declarationOf(data.path), clause);
    return { solver.viewClause(clause), 0 };
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::newDecisionLevel(Solver& solver) {
    prefixes.newDecisionLevel(solver);
    pending.newDecisionLevel(solver);
    representativeTrace.newDecisionLevel(solver);
    promotionTrace.newDecisionLevel(solver);
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::beginBacktrack(Solver& solver) {
    prefixes.beginBacktrack(solver);

    // A promotion is of the level of the link that joined the declarations, which is also the level
    // the word was registered at, so reverting the one reverts the other. The promotions have to go
    // first, they name the pending containments.
    for (TracePosition pendingPos : promotionTrace.backtrackedReverse(solver))
        pending[pendingPos].promoted = false;
    promotionTrace.truncate(solver);

    // The uses naming these entries were registered at the same level, so they are already gone
    for (TracePosition pendingPos : pending.backtrackedPositionsReverse(solver)) {
        auto& elementPending = stateOf(pending[pendingPos].element).pendingPositions;
        VERIFY(!elementPending.empty());
        VERIFY(elementPending.back() == pendingPos);
        elementPending.pop_back();
    }
    pending.truncate(solver);

    for (ElementId element : representativeTrace.backtrackedReverse(solver))
        stateOf(element).representative.reset();
    representativeTrace.truncate(solver);
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::endBacktrack(Solver& solver) {
    prefixes.endBacktrack(solver);
}

template<typename Derived, typename PrefixImpl>
void MemoryLocationSets<Derived, PrefixImpl>::checkInvariances(Solver& solver) {
    prefixes.checkInvariances(solver);

    pending.checkInvariances(solver);
    representativeTrace.checkInvariances(solver);
    promotionTrace.checkInvariances(solver);

    std::vector<std::vector<TracePosition>> expectedIndices;
    std::vector<TracePosition> expectedPromotions;
    expectedIndices.resize(elementStates.size());
    for (TracePosition pendingPos : pending.allPositions()) {
        const PendingContainment& entry = pending[pendingPos];
        VERIFY(entry.element.id() < elementStates.size());
        expectedIndices[entry.element.id()].push_back(pendingPos);
        if (entry.promoted)
            expectedPromotions.push_back(pendingPos);

        // A containment is registered exactly when its declaration is the one of the representative,
        // so anything else here means that a notification was missed
        VERIFY(entry.promoted
            == joinedRepresentative(solver, elementStates[entry.element.id()], locationOf(entry.containment.set()).declaration));
    }

    // An element has a representative exactly when it is on the trace, and it is on it only once
    std::vector<bool> onTrace;
    onTrace.resize(elementStates.size());
    for (ElementId element : representativeTrace) {
        VERIFY(element.id() < onTrace.size());
        VERIFY(!onTrace[element.id()]);
        onTrace[element.id()] = true;
    }

    for (int_t i = 0; i < (int_t)elementStates.size(); i++) {
        VERIFY(elementStates[i].representative.has_value() == onTrace[i]);
        VERIFY(elementStates[i].pendingPositions == expectedIndices[i]);
    }

    std::vector<TracePosition> promotions(promotionTrace.begin(), promotionTrace.end());
    std::ranges::sort(promotions);
    VERIFY(promotions == expectedPromotions);
}

}
