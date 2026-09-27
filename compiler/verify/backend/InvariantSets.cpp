#include <verify/backend/InvariantSets.h>

#include <verify/backend/MemoryLocationSets.impl.h>

#include <algorithm>

namespace verify::backend {

template struct MemoryLocationSets<InvariantSets>;

struct ExactToInclusiveReason {
    SetElement element;
    InvariantSet exactSet;
};

InvariantSets::InvariantSets(Solver& solver)
    : Base(solver), inclusiveInfos(solver), exclusiveInfos(solver), pathInfos(solver), exactInfos(solver) { }

template<typename Info, TheoryId theory>
static InvariantSet locationSet(
    Solver& solver,
    std::unordered_map<MemoryLocation, InvariantSet, MemoryLocationHash>& sets,
    TheoryData<Info, theory>& infos,
    MemoryLocation location) {
    if (theory == TheoryId::PathInvariantSets && location.member == identity_member) {
        return (InvariantSet)solver.emptySet(Sort::InvariantSet);
    }

    auto it = sets.find(location);
    if (it != sets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.newValue(theory);
    infos[newSet].location = location;
    sets.emplace(location, newSet);

    if constexpr (theory == TheoryId::PathInvariantSets) {
        solver.addClause({ !solver.equality(location.member, identity_member), solver.isEmpty(newSet) });
    }

    return newSet;
}

InvariantSet InvariantSets::inclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, inclusiveSets, inclusiveInfos, location);
}

InvariantSet InvariantSets::exclusiveSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, exclusiveSets, exclusiveInfos, location);
}

InvariantSet InvariantSets::pathSet(Solver& solver, MemoryLocation location) {
    return locationSet(solver, pathSets, pathInfos, location);
}

InvariantSet InvariantSets::exactSet(Solver& solver, MemoryLocation location, Invariant invariant) {
    ExactSetKey key { location, invariant };
    auto it = exactSets.find(key);
    if (it != exactSets.end())
        return it->second;

    InvariantSet newSet = (InvariantSet)solver.newValue(TheoryId::ExactInvariantSets);
    exactInfos[newSet].location = location;
    exactInfos[newSet].invariant = invariant;
    exactSets.emplace(key, newSet);
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
    case TheoryId::ExactInvariantSets:
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
    exactSetIndex.propagateRewrite(solver, use);
}

void InvariantSets::propagateContainment(Solver& solver, SetElement element, SetContainment containment) {
    if (element == baseTheory(solver).forAllElement())
        return;

    InvariantSet set = (InvariantSet)containment.set();
    VERIFY(isInvariantSet(set));

    if (set.theory() == TheoryId::ExactInvariantSets) {
        if (containment.contained()) {
            // in exactSet(loc, I) => in inclusiveSet(loc)
            baseTheory(solver).assignTrue(solver, element, Sets::in(inclusiveSet(solver, locationOf(set))),
                makeReason<ReasonKind::InvariantExactToInclusive>({ element, (InvariantSet)containment.set() }));

            // in exact1 and in exact2 => exact1 = exact2
            auto key = exactSetIndex.keyOf(element);
            if (!key.has_value()) {
                exactSetIndex.setKey(solver, element, set);
            } else {
                solver.assignTrue(solver.equality(key.value(), set),
                    makeReason<ReasonKind::InvariantExactSetsShareElement>({ element, key.value(), set }));
            }
        } else {
            // in exactSet(loc1, I) and not in exactSet(loc2, I) => conflict when assignedEqual(loc1, loc2)
            exactSetIndex.addWatch(solver, element, set);
        }
    }

    Base::propagateContainment(solver, element, containment);
}

bool InvariantSets::testReason(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::InvariantExactSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setA))
            && baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(setB));
    } else if (reason.kind() == ReasonKind::InvariantExactToInclusive) {
        auto [element, exactSet] = reason.get<ReasonKind::InvariantExactToInclusive>();
        return baseTheory(solver).assignedTrue(solver, element, Sets::in(exactSet));
    } else if (reason.kind() == ReasonKind::InvariantExactConflict) {
        auto data = reason.getData<SharedElementReason>();
        auto [key, watch] = data.sets();
        VERIFY(assignedLiteral == false_literal);
        return baseTheory(solver).assignedTrue(solver, data.element(), Sets::in(key))
            && baseTheory(solver).assignedTrue(solver, data.element(), !Sets::in(watch))
            && exactSetIndex.matches(solver, data.element(), (InvariantSet)key, (InvariantSet)watch);
    }

    return Base::testReason(solver, assignedLiteral, reason);
}

ClauseAndIndex InvariantSets::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::InvariantExactSetsShareElement) {
        auto data = reason.getData<SharedElementReason>();
        auto [setA, setB] = data.sets();

        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setA)));
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(setB)));
        return { solver.viewClause(clause), 0 };
    } else if (reason.kind() == ReasonKind::InvariantExactToInclusive) {
        auto [element, exactSet] = reason.get<ReasonKind::InvariantExactToInclusive>();
        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, element, !Sets::in(exactSet)));
        return { solver.viewClause(clause), 0 };
    } else if (reason.kind() == ReasonKind::InvariantExactConflict) {
        auto data = reason.getData<SharedElementReason>();
        auto [key, watch] = data.sets();
        VERIFY(assignedLiteral == false_literal);
        ClauseBuilder clause = solver.beginClause();
        clause.add(solver, assignedLiteral);
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), !Sets::in(key)));
        clause.add(solver, baseTheory(solver).mapToBool(solver, data.element(), Sets::in(watch)));
        exactSetIndex.explainMatch(solver, data.element(), (InvariantSet)key, (InvariantSet)watch, clause);
        return { solver.viewClause(clause), 0 };
    }

    return Base::reasonToClause(solver, assignedLiteral, reason);
}

void InvariantSets::newDecisionLevel(Solver& solver) {
    Base::newDecisionLevel(solver);
    exactSetIndex.newDecisionLevel(solver);
}

void InvariantSets::beginBacktrack(Solver& solver) {
    Base::beginBacktrack(solver);
    exactSetIndex.beginBacktrack(solver);
}

void InvariantSets::checkInvariances(Solver& solver) {
    Base::checkInvariances(solver);
    exactSetIndex.checkInvariances(solver);
}

InvariantSets& InvariantSets::ExactSetIndex::invariantSets() {
    return *ReverseMemberPointer<&InvariantSets::exactSetIndex>::reverse(this);
}

void InvariantSets::ExactSetIndex::addValueUses(Solver& solver, SetElement, InvariantSet set, Use use) {
    auto loc = invariantSets().locationOf(set);
    solver.addUse(loc.declaration, use);
    solver.addUse(loc.member, use);
}

bool InvariantSets::ExactSetIndex::matches(Solver& solver, SetElement, InvariantSet key, InvariantSet watch) {
    auto keyLoc = invariantSets().locationOf(key);
    auto watchLoc = invariantSets().locationOf(watch);
    return invariantSets().invariantOf(key) == invariantSets().invariantOf(watch)
        && solver.assignedEqual(keyLoc.declaration, watchLoc.declaration)
        && solver.assignedEqual(keyLoc.member, watchLoc.member);
}

void InvariantSets::ExactSetIndex::explainMatch(Solver& solver, SetElement, InvariantSet key, InvariantSet watch, ClauseBuilder& clause) {
    auto keyLoc = invariantSets().locationOf(key);
    auto watchLoc = invariantSets().locationOf(watch);
    solver.explainEqual(keyLoc.declaration, watchLoc.declaration, clause);
    solver.explainEqual(keyLoc.member, watchLoc.member, clause);
}

void InvariantSets::ExactSetIndex::onKeyMatch(Solver& solver, SetElement element, InvariantSet key, InvariantSet watch) {
    solver.assignTrue(false_literal, makeReason<ReasonKind::InvariantExactConflict>({ element, key, watch }));
}

}
