#pragma once

#include <verify/backend/KeyWatches.impl.h>
#include <verify/backend/MemoryLocationSets.h>
#include <verify/backend/Sets.h>

#include <ReverseMemberPointer.h>

namespace verify::backend {

template<typename Derived>
MemoryLocationSets<Derived>::MemoryLocationSets(Solver&)
    : prefixes(params().prefixParams) { }

template<typename Derived>
Sets& MemoryLocationSets<Derived>::baseTheory(Solver& solver) {
    return solver.setTheory(params().setSort);
}

template<typename Derived>
constexpr MemoryLocationSetsParams MemoryLocationSets<Derived>::params() {
    return Derived::PARAMS;
}

template<typename Derived>
Bool MemoryLocationSets<Derived>::containmentOf(Solver& solver, PrefixIndex::WordId word) {
    return baseTheory(solver).mapToBool(solver, prefixes.elementOf(word), prefixes.containmentOf(word));
}

template<typename Derived>
MemoryLocationSets<Derived>& MemoryLocationSets<Derived>::PendingWatches::locationSets() {
    return *ReverseMemberPointer<&MemoryLocationSets<Derived>::pendingWatches>::reverse(this);
}

template<typename Derived>
bool MemoryLocationSets<Derived>::PendingWatches::matches(Solver& solver, ElementId, Containment key, Containment watch) {
    MemoryLocationSets<Derived>& locations = locationSets();
    return solver.assignedEqual(locations.locationOf(key.set()).declaration, locations.locationOf(watch.set()).declaration);
}

template<typename Derived>
void MemoryLocationSets<Derived>::PendingWatches::addValueUses(Solver& solver, ElementId, Containment watch, Use use) {
    solver.addUse(locationSets().locationOf(watch.set()).declaration, use);
}

template<typename Derived>
void MemoryLocationSets<Derived>::PendingWatches::onKeyMatch(Solver& solver, ElementId element, Containment, Containment watch) {
    locationSets().addWord(solver, element, watch);
}

template<typename Derived>
void MemoryLocationSets<Derived>::addWord(Solver& solver, ElementId element, Containment cont) {
    derived().addWords(solver, prefixes, element, cont);
}

template<typename Derived>
void MemoryLocationSets<Derived>::propagateContainment(Solver& solver, ElementId element, Sets::Containment containment) {
    Set set = containment.set();

    if (element == baseTheory(solver).forAllElement())
        return;

    if (containment.contained()) {
        std::optional<Containment> key = pendingWatches.keyOf(element);
        if (!key.has_value()) {
            VERIFY(solver.currentDecisionLevel() >= 0); // No positive set assignments are made without a decision
            // The set of the first location found to contain the element is its representative, and
            // the containments pending so far were all waiting for one to compare against
            pendingWatches.setKey(solver, element, containment);
        } else {
            // Distinct declarations describe distinct memory, so an element of both locations means
            // that the declarations are the same
            solver.assignTrue(solver.equality(locationOf(key->set()).declaration, locationOf(set).declaration),
                makeReason<params().declarationsShareElementReason>({ element, key->set(), set }));
        }
    }

    pendingWatches.addWatch(solver, element, containment);
}

template<typename Derived>
void MemoryLocationSets<Derived>::propagateRewrite(Solver& solver, Use use) {
    pendingWatches.propagateRewrite(solver, use);
    if (use.kind() == prefixes.params().wordUse)
        prefixes.propagateRewrite(solver, use);
}

template<typename Derived>
bool MemoryLocationSets<Derived>::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == params().declarationsShareElementReason) {
        auto data = reason.get(params().declarationsShareElementReason);
        auto [setA, setB] = data.sets();
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setA))
            && baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setB));
    }

    VERIFY(assignedLiteral == false_literal);
    auto data = reason.get(prefixes.params().hitReason);
    // The locations are only comparable as long as they belong to the same declaration
    return solver.assignedEqual(declarationOf(data.prefix), declarationOf(data.path))
        && prefixes.isConflict(data.prefix, data.path)
        && solver.assignedTrue(containmentOf(solver, data.prefix))
        && solver.assignedTrue(containmentOf(solver, data.path));
}

template<typename Derived>
ClauseAndIndex MemoryLocationSets<Derived>::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
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
    auto data = reason.get(prefixes.params().hitReason);

    ClauseBuilder clause = solver.beginClause();
    clause.add(solver, assignedLiteral);
    // The spelling of the prefix is a prefix of the spelling of the path, which relates their two
    // sets: one of them is a subset of the other, or the two are disjoint. Which of those it is
    // depends on the kinds of the sets, but in every case the two containments cannot hold at the
    // same time, so negating both of them is the clause saying so.
    clause.add(solver, !containmentOf(solver, data.prefix));
    clause.add(solver, !containmentOf(solver, data.path));
    prefixes.explainPrefix(solver, data.prefix, data.path, clause);
    // The subset relation only holds within one declaration
    solver.explainEqual(declarationOf(data.prefix), declarationOf(data.path), clause);
    return { solver.viewClause(clause), 0 };
}

template<typename Derived>
void MemoryLocationSets<Derived>::newDecisionLevel(Solver& solver) {
    prefixes.newDecisionLevel(solver);
    pendingWatches.newDecisionLevel(solver);
}

template<typename Derived>
void MemoryLocationSets<Derived>::beginBacktrack(Solver& solver) {
    prefixes.beginBacktrack(solver);
    pendingWatches.beginBacktrack(solver);
}

template<typename Derived>
void MemoryLocationSets<Derived>::endBacktrack(Solver& solver) {
    prefixes.endBacktrack(solver);
}

template<typename Derived>
void MemoryLocationSets<Derived>::checkInvariances(Solver& solver) {
    prefixes.checkInvariances(solver);
    pendingWatches.checkInvariances(solver);
}

}
