#include <verify/backend/InvariantSets.h>

#include <verify/backend/SolverImpl.h>

#include <algorithm>

namespace verify::backend {

InvariantSets::InvariantSets(Solver& solver)
    : setInfos(solver)
    , prefixes(solver) { }

Sets& InvariantSets::baseTheory(Solver& solver) {
    return solver.impl().invariantSetsBaseTheory;
}

Bool InvariantSets::containmentOf(Solver& solver, InvariantPrefixes::WordId word) {
    return baseTheory(solver).mapToBool(solver, prefixes.elementOf(word), prefixes.containmentOf(word));
}

Value InvariantSets::locationSet(Solver& solver, LocationSets& sets, TheoryId theory, MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    Value newSet = solver.impl().newValue(theory);
    setInfos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

Value InvariantSets::inclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, inclusiveSets, TheoryId::InclusiveLocationInvariantSets, location);
}

Value InvariantSets::exclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, exclusiveSets, TheoryId::ExclusiveLocationInvariantSets, location);
}

Value InvariantSets::leafSet(Solver& solver, MemoryLocation location, Invariant invariant) {
    LeafKey key { location, invariant };
    auto it = leafSets.find(key);
    if (it != leafSets.end())
        return it->second;

    Value newSet = solver.impl().newValue(TheoryId::LeafInvariantSets);
    setInfos[newSet].location = location;
    setInfos[newSet].invariant = invariant;
    leafSets.emplace(key, newSet);
    return newSet;
}

InvariantWord InvariantSets::toWord(Value set) const {
    Member member = locationOf(set).member;
    switch (set.theory()) {
    case TheoryId::InclusiveLocationInvariantSets:
        return InvariantWord::inclusive(member);
    case TheoryId::ExclusiveLocationInvariantSets:
        return InvariantWord::exclusive(member);
    case TheoryId::LeafInvariantSets:
        return InvariantWord::leaf(member, invariantOf(set));
    default:
        VERIFY_NOT_REACHED();
    }
}

bool InvariantSets::joinedRepresentative(Solver& solver, const ElementState& state, MemoryDeclaration declaration) {
    if (!state.representative.has_value())
        return false;
    MemoryDeclaration representative = locationOf(state.representative.value()).declaration;
    return solver.assignedEqual(representative, declaration);
}

void InvariantSets::addWord(Solver& solver, ElementId element, Containment cont) {
    prefixes.addWord(solver, toWord(cont.set()), element, cont);
}

void InvariantSets::addPending(Solver& solver, ElementId element, Containment cont) {
    TracePosition position = pending.push({ .element = element, .containment = cont });
    stateOf(element).pendingPositions.push_back(position);
    solver.addUse(locationOf(cont.set()).declaration, Use(UseKind::InvariantSetPendingContainment, position.index));
}

void InvariantSets::promotePending(Solver& solver, const ElementState& state, TracePosition position) {
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

void InvariantSets::promotePendingOf(Solver& solver, ElementId element) {
    const ElementState& state = stateOf(element);
    for (TracePosition position : state.pendingPositions)
        promotePending(solver, state, position);
}

void InvariantSets::propagateContainment(Solver& solver, ElementId element, Containment containment) {
    Value set = containment.set();
    VERIFY(isInvariantSet(set));

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
            solver.addUse(location.declaration, Use(UseKind::InvariantSetRepresentative, element.id()));
        } else {
            // Distinct declarations describe distinct memory, so a leaf of both locations means that
            // the declarations are the same
            Value representative = state.representative.value();
            solver.assignTrue(solver.equality(locationOf(representative).declaration, location.declaration),
                makeReason<ReasonKind::InvariantDeclarationsShareElement>({ element, representative, set }));
        }

        if (set.theory() == TheoryId::LeafInvariantSets) {
            if (!state.leaf.has_value()) {
                state.leaf = set;
                leafTrace.push(element);
            } else {
                Value leaf = state.leaf.value();
                solver.assignTrue(solver.equality(leaf, set),
                    makeReason<ReasonKind::InvariantLeafSetsShareElement>({ element, leaf, set }));
            }
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

void InvariantSets::propagateRewrite(Solver& solver, Use use) {
    switch (use.kind()) {
    case UseKind::InvariantSetPendingContainment: {
        TracePosition position { use.id() };
        promotePending(solver, stateOf(pending[position].element), position);
        break;
    }
    case UseKind::InvariantSetRepresentative:
        promotePendingOf(solver, ElementId(use.id()));
        break;
    case UseKind::InvariantPrefixWord:
        prefixes.propagateRewrite(solver, use);
        break;
    default:
        VERIFY_NOT_REACHED();
    }
}

bool InvariantSets::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    // Both conclusions are drawn from the same two containments, only what they say differs
    if (reason.kind() == ReasonKind::InvariantDeclarationsShareElement
        || reason.kind() == ReasonKind::InvariantLeafSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setA))
            && baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setB));
    }

    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get<ReasonKind::InvariantPrefixHit>();
    // The locations are only comparable as long as they belong to the same declaration
    return solver.assignedEqual(declarationOf(data.prefix), declarationOf(data.path))
        && prefixes.isPrefixOf(data.prefix, data.path)
        && solver.assignedTrue(containmentOf(solver, data.prefix))
        && solver.assignedTrue(containmentOf(solver, data.path));
}

ClauseAndIndex InvariantSets::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::InvariantDeclarationsShareElement
        || reason.kind() == ReasonKind::InvariantLeafSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();

        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setA)));
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setB)));
        return { solver.viewClause(clause), 0 };
    }

    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get<ReasonKind::InvariantPrefixHit>();

    ClauseBuilder clause = solver.beginClause();
    clause.add(solver, assignedLiteral);
    // The word of the prefix spells a way the word of the path passes as well, which makes the set
    // of the path a subset of the set of the prefix. So the element cannot be excluded from the one
    // and contained in the other at the same time.
    clause.add(solver, !containmentOf(solver, data.prefix));
    clause.add(solver, !containmentOf(solver, data.path));
    prefixes.explainPrefix(solver, data.prefix, data.path, clause);
    // The subset relation only holds within one declaration
    solver.impl().memoryDeclarationEquality.explainEqual(solver, declarationOf(data.prefix), declarationOf(data.path), clause);
    return { solver.viewClause(clause), 0 };
}

void InvariantSets::newDecisionLevel(Solver& solver) {
    prefixes.newDecisionLevel(solver);
    pending.newDecisionLevel(solver);
    representativeTrace.newDecisionLevel(solver);
    leafTrace.newDecisionLevel(solver);
    promotionTrace.newDecisionLevel(solver);
}

void InvariantSets::beginBacktrack(Solver& solver) {
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

    for (ElementId element : leafTrace.backtrackedReverse(solver))
        stateOf(element).leaf.reset();
    leafTrace.truncate(solver);
}

void InvariantSets::checkInvariances(Solver& solver) {
    prefixes.checkInvariances(solver);

    pending.checkInvariances(solver);
    representativeTrace.checkInvariances(solver);
    leafTrace.checkInvariances(solver);
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

    // An element has a representative exactly when it is on the trace, and it is on it only once.
    // The same holds for the leaf set it was found in.
    std::vector<bool> onRepresentativeTrace;
    std::vector<bool> onLeafTrace;
    onRepresentativeTrace.resize(elementStates.size());
    onLeafTrace.resize(elementStates.size());
    auto markOnTrace = [](std::vector<bool>& marks, ElementId element) {
        VERIFY(element.id() < marks.size());
        VERIFY(!marks[element.id()]);
        marks[element.id()] = true;
    };
    for (ElementId element : representativeTrace)
        markOnTrace(onRepresentativeTrace, element);
    for (ElementId element : leafTrace)
        markOnTrace(onLeafTrace, element);

    for (int_t i = 0; i < (int_t)elementStates.size(); i++) {
        VERIFY(elementStates[i].representative.has_value() == onRepresentativeTrace[i]);
        VERIFY(elementStates[i].leaf.has_value() == onLeafTrace[i]);
        VERIFY(elementStates[i].pendingPositions == expectedIndices[i]);
    }

    std::vector<TracePosition> promotions(promotionTrace.begin(), promotionTrace.end());
    std::ranges::sort(promotions);
    VERIFY(promotions == expectedPromotions);
}

}
