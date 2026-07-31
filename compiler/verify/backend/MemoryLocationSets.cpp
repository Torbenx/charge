#include <verify/backend/MemoryLocationSets.h>

#include <verify/backend/SolverImpl.h>

#include <algorithm>

namespace verify::backend {

namespace {
    struct SetPair {
        Value setA;
        Value setB;
    };
}

//! An element contained in the sets of two locations, which makes their declarations equal
struct SharedElementReason : private PackedReason<SetPair, uint32_t> {
    SharedElementReason(Sets::ElementId element, Value setA, Value setB)
        : PackedReason({ setA, setB }, element.id()) { }

    SetPair sets() const { return data(); }
    Sets::ElementId element() const { return Sets::ElementId(tag()); }
};

MemoryLocationSets::MemoryLocationSets(Solver& solver)
    : setInfos(solver)
    , prefixes(solver) { }

Value MemoryLocationSets::set(Solver& solver, MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    Value newSet = solver.impl().newValue(TheoryId::MemoryLocationSets);
    setInfos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

Sets& MemoryLocationSets::memorySets(Solver& solver) {
    return solver.impl().memorySets;
}

Bool MemoryLocationSets::containmentOf(Solver& solver, MemberPrefixes::WordId word) {
    return memorySets(solver).mapToBool(solver, prefixes.elementOf(word), prefixes.containmentOf(word));
}

bool MemoryLocationSets::joinedRepresentative(Solver& solver, const ElementState& state, MemoryDeclaration declaration) {
    if (!state.representative.has_value())
        return false;
    MemoryDeclaration representative = locationOf(state.representative.value()).declaration;
    return solver.assignedEqual(representative, declaration);
}

void MemoryLocationSets::addWord(Solver& solver, ElementId element, Containment cont) {
    Member member = locationOf(cont.set()).member;
    prefixes.addWord(solver, member, element, cont);
}

void MemoryLocationSets::addPending(Solver& solver, ElementId element, Containment cont) {
    TracePosition position = pending.push({ .element = element, .containment = cont });
    stateOf(element).pendingPositions.push_back(position);
    solver.addUse(locationOf(cont.set()).declaration, Use(UseKind::MemoryLocationSetPendingContainment, position.index));
}

void MemoryLocationSets::promotePending(Solver& solver, const ElementState& state, TracePosition position) {
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

void MemoryLocationSets::promotePendingOf(Solver& solver, ElementId element) {
    const ElementState& state = stateOf(element);
    for (TracePosition position : state.pendingPositions)
        promotePending(solver, state, position);
}

void MemoryLocationSets::propagateContainment(Solver& solver, ElementId element, Sets::Containment containment) {
    Value set = containment.set();
    VERIFY(set.theory() == TheoryId::MemoryLocationSets);

    if (element == memorySets(solver).forAllElement())
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
            solver.addUse(location.declaration, Use(UseKind::MemoryLocationSetRepresentative, element.id()));
        } else {
            // Distinct declarations describe distinct memory, so an element of both locations means
            // that the declarations are the same
            Value representative = state.representative.value();
            solver.assignTrue(solver.equality(locationOf(representative).declaration, location.declaration),
                makeReason<ReasonKind::MemoryDeclarationsShareElement>({ element, representative, set }));
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

void MemoryLocationSets::propagateRewrite(Solver& solver, Use use) {
    switch (use.kind()) {
    case UseKind::MemoryLocationSetPendingContainment: {
        TracePosition position { use.id() };
        promotePending(solver, stateOf(pending[position].element), position);
        break;
    }
    case UseKind::MemoryLocationSetRepresentative:
        promotePendingOf(solver, ElementId(use.id()));
        break;
    case UseKind::MemberPrefixWord:
        prefixes.propagateRewrite(solver, use);
        break;
    default:
        VERIFY_NOT_REACHED();
    }
}

bool MemoryLocationSets::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::MemoryDeclarationsShareElement) {
        auto data = reason.get<ReasonKind::MemoryDeclarationsShareElement>();
        auto [setA, setB] = data.sets();
        return memorySets(solver).assignedTrue(solver, data.element(), Sets::in(setA))
            && memorySets(solver).assignedTrue(solver, data.element(), Sets::in(setB));
    }

    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get<ReasonKind::MemberPrefixHit>();
    // The locations are only comparable as long as they belong to the same declaration
    return solver.assignedEqual(declarationOf(data.prefix), declarationOf(data.path))
        && prefixes.isPrefixOf(data.prefix, data.path)
        && solver.assignedTrue(containmentOf(solver, data.prefix))
        && solver.assignedTrue(containmentOf(solver, data.path));
}

ClauseAndIndex MemoryLocationSets::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::MemoryDeclarationsShareElement) {
        auto data = reason.get<ReasonKind::MemoryDeclarationsShareElement>();
        auto [setA, setB] = data.sets();

        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, memorySets(solver).mapToBool(solver, data.element(), !Sets::in(setA)));
        clause.add(solver, memorySets(solver).mapToBool(solver, data.element(), !Sets::in(setB)));
        return { solver.viewClause(clause), 0 };
    }

    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get<ReasonKind::MemberPrefixHit>();

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

void MemoryLocationSets::newDecisionLevel(Solver& solver) {
    prefixes.newDecisionLevel(solver);
    pending.newDecisionLevel(solver);
    representativeTrace.newDecisionLevel(solver);
    promotionTrace.newDecisionLevel(solver);
}

void MemoryLocationSets::beginBacktrack(Solver& solver) {
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

void MemoryLocationSets::checkInvariances(Solver& solver) {
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
