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
    return memorySets(solver).mapToBool(solver, prefixes.elementOf(word), prefixes.payloadOf(word));
}

bool MemoryLocationSets::joinedRepresentative(Solver& solver, const ElementState& state, MemoryDeclaration declaration) {
    if (!state.representative.has_value())
        return false;
    MemoryDeclaration representative = locationOf(state.representative.value()).declaration;
    return solver.assignedEqual(representative, declaration);
}

void MemoryLocationSets::addWord(Solver& solver, ElementId element, Containment cont) {
    Member member = locationOf(cont.set()).member;
    prefixes.addWord(solver, element, member, cont);
}

void MemoryLocationSets::addPending(Solver& solver, ElementId element, Containment cont) {
    uint32_t index = pending.size();
    pending.push_back({ .element = element, .containment = cont });
    stateOf(element).pendingIndices.push_back(index);

    // The declaration may be joined to the one of the representative from either side, so both are
    // watched. Only the tree that is linked below the other one is notified, so exactly one of the
    // two uses reports the join.
    solver.addUse(locationOf(cont.set()).declaration, Use(UseKind::MemoryLocationSetPendingContainment, index));
}

void MemoryLocationSets::promotePending(Solver& solver, const ElementState& state, uint32_t index) {
    PendingContainment& entry = pending[index];
    if (entry.promoted)
        return;
    if (!joinedRepresentative(solver, state, locationOf(entry.containment.set()).declaration))
        return;

    // The link that joined the declarations is of the current decision level, so the word registered
    // here is unregistered again by the same backtrack that reverts the promotion.
    entry.promoted = true;
    promotionTrace.push_back(index);
    addWord(solver, entry.element, entry.containment);
}

void MemoryLocationSets::promotePendingOf(Solver& solver, ElementId element) {
    // Note: promotePending() does not add pending containments, so the indices are stable here
    const ElementState& state = stateOf(element);
    for (int_t i = 0; i < (int_t)state.pendingIndices.size(); i++)
        promotePending(solver, state, state.pendingIndices[i]);
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
            // The containment is propagated exactly once per decision level it is assigned at, so
            // everything recorded here is reverted again by the same backtrack that reverts the
            // assignment.
            state.representative = set;
            representativeTrace.push_back(element);
            newRepresentative = true;

            // The declarations of the pending containments are compared against this one, so any of
            // them may be joined when its representative changes
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
        uint32_t index = use.id();
        promotePending(solver, stateOf(pending[index].element), index);
        break;
    }
    case UseKind::MemoryLocationSetRepresentative:
        // The representative moved, which may have joined any of the pending declarations
        promotePendingOf(solver, ElementId(use.id()));
        break;
    case UseKind::MemberPrefixWord:
        // The normal form of the member of a location changed
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
    pendingDecisionPoints.push_back(pending.size());
    representativeDecisionPoints.push_back(representativeTrace.size());
    promotionDecisionPoints.push_back(promotionTrace.size());
    VERIFY((int_t)pendingDecisionPoints.size() == solver.currentDecisionLevel() + 1);
}

void MemoryLocationSets::beginBacktrack(Solver& solver) {
    prefixes.beginBacktrack(solver);

    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;

    // A promotion is of the level of the link that joined the declarations, which is also the level
    // the word was registered at, so reverting the one reverts the other
    int_t promotionTargetSize = promotionDecisionPoints[lastLevelToRevert];
    while ((int_t)promotionTrace.size() > promotionTargetSize) {
        pending[promotionTrace.back()].promoted = false;
        promotionTrace.pop_back();
    }
    promotionDecisionPoints.resize(lastLevelToRevert);

    // The uses naming these entries were registered at the same level, so they are already gone
    int_t pendingTargetSize = pendingDecisionPoints[lastLevelToRevert];
    while ((int_t)pending.size() > pendingTargetSize) {
        auto& indices = stateOf(pending.back().element).pendingIndices;
        VERIFY(!indices.empty());
        VERIFY(indices.back() == pending.size() - 1);
        indices.pop_back();
        pending.pop_back();
    }
    pendingDecisionPoints.resize(lastLevelToRevert);

    int_t representativeTargetSize = representativeDecisionPoints[lastLevelToRevert];
    while ((int_t)representativeTrace.size() > representativeTargetSize) {
        stateOf(representativeTrace.back()).representative.reset();
        representativeTrace.pop_back();
    }
    representativeDecisionPoints.resize(lastLevelToRevert);
}

void MemoryLocationSets::checkInvariances(Solver& solver) {
    prefixes.checkInvariances(solver);

    // The entries of a level are appended after its decision point, so the points only grow
    auto checkDecisionPoints = [&solver](const std::vector<uint32_t>& points, size_t traceSize) {
        VERIFY((int_t)points.size() == solver.currentDecisionLevel() + 1);
        VERIFY(std::ranges::is_sorted(points));
        VERIFY(points.empty() || points.back() <= traceSize);
    };
    checkDecisionPoints(pendingDecisionPoints, pending.size());
    checkDecisionPoints(representativeDecisionPoints, representativeTrace.size());
    checkDecisionPoints(promotionDecisionPoints, promotionTrace.size());

    std::vector<std::vector<uint32_t>> expectedIndices;
    std::vector<uint32_t> expectedPromotions;
    expectedIndices.resize(elementStates.size());
    for (uint32_t index = 0; index < pending.size(); index++) {
        const PendingContainment& entry = pending[index];
        VERIFY(entry.element.id() < elementStates.size());
        expectedIndices[entry.element.id()].push_back(index);
        if (entry.promoted)
            expectedPromotions.push_back(index);

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
        VERIFY(elementStates[i].pendingIndices == expectedIndices[i]);
    }

    std::vector<uint32_t> promotions = promotionTrace;
    std::ranges::sort(promotions);
    VERIFY(promotions == expectedPromotions);
}

}
