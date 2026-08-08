#include <verify/backend/InvariantSets.h>

#include <verify/backend/MemoryLocationSets.impl.h>

#include <algorithm>

namespace verify::backend {

template struct MemoryLocationSets<InvariantSets>;

struct SingletonToInclusiveReason {
    SetElement element;
    InvariantSet singletonSet;
};

InvariantSets::InvariantSets(Solver& solver)
    : Base(solver), inclusiveInfos(solver), exclusiveInfos(solver), pathInfos(solver), singletonInfos(solver) { }

template<typename Info, TheoryId theory>
static InvariantSet locationSet(
    Solver& solver,
    std::unordered_map<MemoryLocation, InvariantSet, MemoryLocationHash>& sets,
    TheoryData<Info, theory>& infos,
    MemoryLocation location) {
    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.newValue(theory);
    infos[newSet].location = location;
    sets.emplace(location, newSet);
    return newSet;
}

InvariantSet InvariantSets::inclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, inclusiveSets, inclusiveInfos, location);
}

InvariantSet InvariantSets::exclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, exclusiveSets, exclusiveInfos, location);
}

InvariantSet InvariantSets::pathSet(Solver& solver, MemoryLocation location) {
    int_t oldCount = pathSets.size();
    InvariantSet set = locationSet(solver, pathSets, pathInfos, location);

    // No location is strictly above the whole declaration, so its path set is the empty one. Without
    // this the emptiness is only found once the element is known to be somewhere in the declaration.
    // TODO: There may be problem with elements that are later rewritten to the identity which are
    //       not caught here.
    if ((int_t)pathSets.size() != oldCount && location.member == identity_member)
        solver.addClause({ baseTheory(solver).isEmpty(solver, set) });

    return set;
}

InvariantSet InvariantSets::singletonSet(Solver& solver, MemoryLocation location, Invariant invariant) {
    SingletonKey key { location, invariant };
    auto it = singletonSets.find(key);
    if (it != singletonSets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.newValue(TheoryId::InvariantSingletonSets);
    singletonInfos[newSet].location = location;
    singletonInfos[newSet].invariant = invariant;
    singletonSets.emplace(key, newSet);
    return newSet;
}

void InvariantSets::addWords(Solver& solver, PrefixIndex& prefixes, SetElement element, SetContainment cont) {
    PrefixIndex::Role role;
    PrefixIndex::SelfInclusion inclusion;
    switch (cont.set().theory()) {
    case TheoryId::InclusiveLocationInvariantSets:
        role = cont.contained() ? PrefixIndex::Role::Path : PrefixIndex::Role::Prefix;
        inclusion = PrefixIndex::SelfInclusion::Inclusive;
        break;
    case TheoryId::ExclusiveLocationInvariantSets:
        role = cont.contained() ? PrefixIndex::Role::Path : PrefixIndex::Role::Prefix;
        inclusion = PrefixIndex::SelfInclusion::Exclusive;
        break;
    case TheoryId::PathInvariantSets:
        role = cont.contained() ? PrefixIndex::Role::Prefix : PrefixIndex::Role::Path;
        inclusion = PrefixIndex::SelfInclusion::Inclusive;
        break;
    case TheoryId::InvariantSingletonSets:
        if (!cont.contained())
            return;
        role = PrefixIndex::Role::Prefix;
        inclusion = PrefixIndex::SelfInclusion::Exclusive;
        break;
    default:
        VERIFY_NOT_REACHED();
    }
    Member member = locationOf((InvariantSet)cont.set()).member;
    prefixes.addWord(solver, member, element, cont, role, inclusion);
}

void InvariantSets::propagateRewrite(Solver& solver, Use use) {
    Base::propagateRewrite(solver, use);
    singletonIndex.propagateRewrite(solver, use);
}

void InvariantSets::propagateContainment(Solver& solver, SetElement element, SetContainment containment) {
    if (element == baseTheory(solver).forAllElement())
        return;

    InvariantSet set = (InvariantSet)containment.set();
    VERIFY(isInvariantSet(set));

    if (set.theory() == TheoryId::InvariantSingletonSets) {
        if (containment.contained()) {
            // in singletonSet(loc, I) => in inclusiveSet(loc)
            baseTheory(solver).assignTrue(solver, element, Sets::in(inclusiveSet(solver, locationOf(set))),
                makeReason<ReasonKind::InvariantSingletonToInclusive>({ element, (InvariantSet)containment.set() }));

            // in singleton1 and in singleton2 => singleton1 = singleton2
            auto key = singletonIndex.keyOf(element);
            if (!key.has_value()) {
                singletonIndex.setKey(solver, element, set);
            } else {
                solver.assignTrue(solver.equality(key.value(), set),
                    makeReason<ReasonKind::InvariantSingletonSetsShareElement>({ element, key.value(), set }));
            }
        } else {
            // in singletonSet(loc1, I) and not in singletonSet(loc2, I) => conflcit when assignedEqual(loc1, loc2)
            singletonIndex.addWatch(solver, element, set);
        }
    }

    Base::propagateContainment(solver, element, containment);
}

bool InvariantSets::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::InvariantSingletonSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setA))
            && baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setB));
    } else if (reason.kind() == ReasonKind::InvariantSingletonToInclusive) {
        auto [element, singleton] = reason.get<ReasonKind::InvariantSingletonToInclusive>();
        return baseTheory(solver).assignedTrue(solver, element, Sets::in(singleton));
    } else if (reason.kind() == ReasonKind::InvariantSingletonConflict) {
        auto data = reason.getData<SharedElementReason>();
        auto [key, watch] = data.sets();
        VERIFY(assignedLiteral == false_literal);
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(key))
            && baseTheory(solver).assignedTrue(solver, data.element(), !Sets::in(watch))
            && singletonIndex.matches(solver, data.element(), (InvariantSet)key, (InvariantSet)watch);
    }

    return Base::testReason(solver, assignedLiteral, reason);
}

ClauseAndIndex InvariantSets::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::InvariantSingletonSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();

        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setA)));
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setB)));
        return { solver.viewClause(clause), 0 };
    } else if (reason.kind() == ReasonKind::InvariantSingletonToInclusive) {
        auto [element, singleton] = reason.get<ReasonKind::InvariantSingletonToInclusive>();
        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, element, !Sets::in(singleton)));
        return { solver.viewClause(clause), 0 };
    } else if (reason.kind() == ReasonKind::InvariantSingletonConflict) {
        auto data = reason.getData<SharedElementReason>();
        auto [key, watch] = data.sets();
        VERIFY(assignedLiteral == false_literal);
        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(key)));
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), Sets::in(watch)));
        singletonIndex.explainMatch(solver, data.element(), (InvariantSet)key, (InvariantSet)watch, clause);
        return { solver.viewClause(clause), 0 };
    }

    return Base::reasonToClause(solver, assignedLiteral, reason);
}

void InvariantSets::newDecisionLevel(Solver& solver) {
    Base::newDecisionLevel(solver);
    singletonIndex.newDecisionLevel(solver);
}

void InvariantSets::beginBacktrack(Solver& solver) {
    Base::beginBacktrack(solver);
    singletonIndex.beginBacktrack(solver);
}

void InvariantSets::checkInvariances(Solver& solver) {
    Base::checkInvariances(solver);
    singletonIndex.checkInvariances(solver);
}

InvariantSets& InvariantSets::SingletonIndex::invariantSets() {
    return *ReverseMemberPointer<&InvariantSets::singletonIndex>::reverse(this);
}

void InvariantSets::SingletonIndex::addValueUses(Solver& solver, SetElement, InvariantSet set, Use use) {
    auto loc = invariantSets().locationOf(set);
    solver.addUse(loc.declaration, use);
    solver.addUse(loc.member, use);
}

bool InvariantSets::SingletonIndex::matches(Solver& solver, SetElement, InvariantSet key, InvariantSet watch) {
    auto keyLoc = invariantSets().locationOf(key);
    auto watchLoc = invariantSets().locationOf(watch);
    return invariantSets().invariantOf(key) == invariantSets().invariantOf(watch)
        && solver.assignedEqual(keyLoc.declaration, watchLoc.declaration)
        && solver.assignedEqual(keyLoc.member, watchLoc.member);
}

void InvariantSets::SingletonIndex::explainMatch(Solver& solver, SetElement, InvariantSet key, InvariantSet watch, ClauseBuilder& clause) {
    auto keyLoc = invariantSets().locationOf(key);
    auto watchLoc = invariantSets().locationOf(watch);
    solver.explainEqual(keyLoc.declaration, watchLoc.declaration, clause);
    solver.explainEqual(keyLoc.member, watchLoc.member, clause);
}

void InvariantSets::SingletonIndex::onKeyMatch(Solver& solver, SetElement element, InvariantSet key, InvariantSet watch) {
    solver.assignTrue(false_literal, makeReason<ReasonKind::InvariantSingletonConflict>({ element, key, watch }));
}

}
