#pragma once

#include <verify/backend/KeyWatches.h>

namespace verify::backend {

template<typename Derived, typename KeyType, typename WatchType>
Derived& KeyWatches<Derived, KeyType, WatchType>::derived() { return static_cast<Derived&>(*this); }

template<typename Derived, typename KeyType, typename WatchType>
KeyWatches<Derived, KeyType, WatchType>::ElementState& KeyWatches<Derived, KeyType, WatchType>::stateOf(SetElement element) {
    if (element.id() >= elementStates.size())
        elementStates.resize(element.id() + 1);
    return elementStates[element.id()];
}

template<typename Derived, typename KeyType, typename WatchType>
bool KeyWatches<Derived, KeyType, WatchType>::matches(Solver& solver, SetElement, KeyType key, WatchType watch) { return solver.assignedEqual(key, watch); }

template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::addValueUses(Solver& solver, SetElement, WatchType watch, Use use) { solver.addUse(watch, use); }

template<typename Derived, typename KeyType, typename WatchType>
std::optional<KeyType> KeyWatches<Derived, KeyType, WatchType>::keyOf(SetElement element) const {
    if (element.id() >= elementStates.size())
        return std::nullopt;
    return elementStates[element.id()].key;
}

template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::setKey(Solver& solver, SetElement element, KeyType key) {
    ElementState& state = stateOf(element);
    VERIFY(!state.key.has_value());
    state.key = key;
    keyTrace.push(element);
    derived().addValueUses(solver, element, key, Use(params().keyUse, element.id()));

    // The watches added so far were all waiting for a key to compare against
    reportMatchesOf(solver, element);
}

template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::addWatch(Solver& solver, SetElement element, WatchType watch) {
    WatchId id = watches.push({ .element = element, .value = watch });
    ElementState& state = stateOf(element);
    state.watchIds.push_back(id);
    derived().addValueUses(solver, element, watch, Use(params().watchUse, id.id()));

    reportMatch(solver, element, id);
}

template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::reportMatch(Solver& solver, SetElement element, WatchId watch) {
    WatchEntry& entry = watches[watch];
    const auto& state = stateOf(element);
    if (entry.matched)
        return;
    if (!state.key.has_value())
        return;
    if (!derived().matches(solver, element, state.key.value(), entry.value))
        return;

    // The assignment that established the match is of the current decision level, so whatever the
    // callback registers is unregistered again by the same backtrack that reverts the match.
    entry.matched = true;
    matchTrace.push(watch);
    derived().onKeyMatch(solver, element, state.key.value(), entry.value);
}

//! Bring the key or the watch named by \p use up to date with the rewrites applied
template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::reportMatchesOf(Solver& solver, SetElement element) {
    for (WatchId watch : stateOf(element).watchIds)
        reportMatch(solver, element, watch);
}

template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::propagateRewrite(Solver& solver, Use use) {
    if (use.kind() == params().keyUse) {
        reportMatchesOf(solver, SetElement(use.id()));
    } else if (use.kind() == params().watchUse) {
        WatchId watch { use.id() };
        reportMatch(solver, watches[watch].element, watch);
    }
}

template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::newDecisionLevel(Solver& solver) {
    watches.newDecisionLevel(solver);
    keyTrace.newDecisionLevel(solver);
    matchTrace.newDecisionLevel(solver);
}

template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::beginBacktrack(Solver& solver) {
    // A match is of the level of the assignment that established it, which is also the level the
    // callback acted on, so reverting the one reverts the other. The matches have to go first, they
    // name the watches.
    for (WatchId watch : matchTrace.backtrackedReverse(solver))
        watches[watch].matched = false;
    matchTrace.truncate(solver);

    // The uses naming these entries were registered at the same level, so they are already gone
    for (WatchId watch : watches.backtrackedPositionsReverse(solver)) {
        auto& elementWatches = stateOf(watches[watch].element).watchIds;
        VERIFY(!elementWatches.empty());
        VERIFY(elementWatches.back() == watch);
        elementWatches.pop_back();
    }
    watches.truncate(solver);

    for (SetElement element : keyTrace.backtrackedReverse(solver))
        stateOf(element).key.reset();
    keyTrace.truncate(solver);
}

template<typename Derived, typename KeyType, typename WatchType>
void KeyWatches<Derived, KeyType, WatchType>::checkInvariances(Solver& solver) {
    watches.checkInvariances(solver);
    keyTrace.checkInvariances(solver);
    matchTrace.checkInvariances(solver);

    std::vector<std::vector<WatchId>> expectedWatchIds;
    std::vector<WatchId> expectedMatches;
    expectedWatchIds.resize(elementStates.size());
    for (WatchId watch : watches.allPositions()) {
        const WatchEntry& entry = watches[watch];
        VERIFY(entry.element.id() < elementStates.size());
        expectedWatchIds[entry.element.id()].push_back(watch);
        if (entry.matched)
            expectedMatches.push_back(watch);

        // A match is reported exactly when the watch matches the key, so anything else here means
        // that a notification was missed
        const ElementState& state = elementStates[entry.element.id()];
        VERIFY(entry.matched
            == (state.key.has_value() && derived().matches(solver, entry.element, state.key.value(), entry.value)));
    }

    // An element has a key exactly when it is on the trace, and it is on it only once
    std::vector<bool> onTrace;
    onTrace.resize(elementStates.size());
    for (SetElement element : keyTrace) {
        VERIFY(element.id() < onTrace.size());
        VERIFY(!onTrace[element.id()]);
        onTrace[element.id()] = true;
    }

    for (int_t i = 0; i < (int_t)elementStates.size(); i++) {
        VERIFY(elementStates[i].key.has_value() == onTrace[i]);
        VERIFY(elementStates[i].watchIds == expectedWatchIds[i]);
    }

    std::vector<WatchId> reportedMatches(matchTrace.begin(), matchTrace.end());
    std::ranges::sort(reportedMatches);
    VERIFY(reportedMatches == expectedMatches);
}

}