#include <verify/backend/Members.h>

#include <verify/backend/SolverImpl.h>

#include <algorithm>
#include <queue>

namespace verify::backend {

void Members::reduce(std::vector<Member>& a, std::vector<Member>& b) {
    while (!a.empty() && !b.empty() && a.back() == b.back()) {
        a.pop_back();
        b.pop_back();
    }
    auto aIt = a.begin();
    auto bIt = b.begin();
    while (aIt != a.end() && bIt != b.end() && *aIt == *bIt) {
        ++aIt;
        ++bIt;
    }
    a.erase(a.begin(), aIt);
    b.erase(b.begin(), bIt);
}

uint32_t Members::CompositeMembers::get(Solver& solver, std::vector<Member> expr) {
    return Base::get(solver, expr);
}

std::strong_ordering Members::CompositeMembers::compare(Solver& solver, std::span<const Member> a, std::span<const Member> b) {
    return solver.impl().members.rewriteOrder(solver, a, b);
}

uint32_t Members::CompositeMembers::makeNode(Solver&, std::vector<Member> expr, TreeLabel label) {
    return Base::makeNode(label, std::move(expr));
}

Members::Members(Solver& solver)
    : pairs(solver)
    , variables(solver) {
    VERIFY(compositeMembers.size() == 0);
    uint32_t id = compositeMembers.get(solver, {});
    VERIFY(compositeMembers.size() == 1);
    VERIFY(id == identity_member.id());
    Value identity = solver.impl().newValue(TheoryId::CompositeMembers);
    VERIFY(identity == identity_member);
}

Member Members::compose(Solver& solver, std::span<const Member> expr) {
    if (expr.empty())
        return identity_member;
    if (expr.size() == 1)
        return expr.front();

    std::vector<Member> result;
    for (Member m : expr) {
        if (m.composite()) {
            auto e = compositeMember(m);
            result.insert(result.end(), e.begin(), e.end());
        } else {
            result.push_back(m);
        }
    }

    if (result.empty())
        return identity_member;
    if (result.size() == 1)
        return result.front();

    int_t oldSize = compositeMembers.size();
    uint32_t id = compositeMembers.get(solver, std::move(result));
    if (compositeMembers.size() != oldSize) {
        VERIFY(solver.valueCount(TheoryId::CompositeMembers) == oldSize);
        solver.impl().newValue(TheoryId::CompositeMembers);
    }
    return Member(TheoryId::CompositeMembers, id);
}

std::strong_ordering Members::rewriteOrder(Solver& solver, std::span<const Member> a, std::span<const Member> b) {
    // Rewrite towards longer expressions
    auto sizeOrdering = b.size() <=> a.size();
    if (sizeOrdering != 0)
        return sizeOrdering;

    for (int_t i = 0; i < (int_t)a.size(); i++) {
        if (a[i] != b[i])
            return solver.rewriteOrder(a[i], b[i]);
    }
    return std::strong_ordering::equal;
}

std::vector<Member> Members::rewrite(Member m) {
    if (m.literal())
        return { m };
    if (!m.composite())
        return infoFor(m).currentRewrite;

    std::vector<Member> result;
    for (Member mp : compositeMember(m)) {
        if (mp.literal()) {
            result.push_back(mp);
        } else {
            auto& varInfo = infoFor(mp);
            result.insert(result.end(), varInfo.currentRewrite.begin(), varInfo.currentRewrite.end());
        }
    }
    return result;
}

void Members::markUsesAsDirty(VariableInfo& varInfo) {
    for (RewriteTracePosition use : varInfo.rewriteUses) {
        VERIFY(!varInfo.tracePos.has_value() || use < varInfo.tracePos.value());
        dirtyRewrites.push(use);
    }
    for (PairHandle pair : varInfo.pairUses) {
        dirtyPairs.push(pair);
    }
}

void Members::addRewrite(Member target, PairHandle pair, std::vector<Member> expression) {
    println("add rewrite from {}", pair.pairId());
    auto& varInfo = infoFor(target);
    RewriteTracePosition tracePos(rewriteTrace.size());
    rewriteTrace.push_back({ { target }, pair });
    pairs[makeEquality(pair)].rewrite = tracePos;
    for (Member m : expression) {
        if (!m.literal())
            infoFor(m).rewriteUses.push_back(tracePos);
    }
    varInfo.currentRewrite = expression;
    varInfo.rewriteExpression = std::move(expression);
    varInfo.tracePos = tracePos;
    markUsesAsDirty(varInfo);
}

void Members::addIdentityRewrite(std::vector<Member> targets, PairHandle pair) {
    println("add identity rewrite from {}", pair.pairId());
    RewriteTracePosition tracePos(rewriteTrace.size());
    pairs[makeEquality(pair)].rewrite = tracePos;
    for (Member target : targets) {
        auto& varInfo = infoFor(target);
        varInfo.currentRewrite.clear();
        varInfo.rewriteExpression.clear();
        varInfo.tracePos = tracePos;
        markUsesAsDirty(varInfo);
    }
    rewriteTrace.push_back({ std::move(targets), pair });
}

void Members::updateRewrite(VariableInfo& varInfo) {
    println("update rewrite {}", rewriteTrace[varInfo.tracePos.value().index].rewritePair.pairId());
    auto& rw = varInfo.currentRewrite;
    rw.clear();
    for (Member m : varInfo.rewriteExpression) {
        if (m.literal()) {
            rw.push_back(m);
        } else {
            auto& otherVarInfo = infoFor(m);
            rw.insert(rw.end(), otherVarInfo.currentRewrite.begin(), otherVarInfo.currentRewrite.end());
        }
    }
    markUsesAsDirty(varInfo);
}

void Members::assignEqual(Solver& solver, PairHandle handle) {
    AssignedPairTracePosition tracePos(assignedPairTrace.size());
    assignedPairTrace.push_back(handle);
    auto equality = makeEquality(handle);
    pairs[equality].equality = tracePos;
    solver.assignTrue(equality, makeReason<ReasonKind::MemberEquality>({}));
}

void Members::assignDisequal(Solver& solver, PairHandle handle) {
    println("decide disequal {}", handle.pairId());
    AssignedPairTracePosition tracePos(assignedPairTrace.size());
    assignedPairTrace.push_back(handle);
    auto equality = makeEquality(handle);
    pairs[equality].disequality = tracePos;
    solver.assignTrue(!equality, makeReason<ReasonKind::MemberDisequality>({}));
}

void Members::updatePair(Solver& solver, PairHandle handle) {
    println("update pair {}", handle.pairId());
    VERIFY(dirtyRewrites.empty());
    BooleanValue equality(encodePairTheoryValue<TheoryId::MemberEquality>(handle));
    if (pairs[equality].assignedOrRewritten())
        return;

    auto [a, b] = solver.at(handle);
    auto aRW = rewrite((Member)a);
    auto bRW = rewrite((Member)b);
    reduce(aRW, bRW);

    auto ordering = rewriteOrder(solver, aRW, bRW);
    if (ordering == 0) {
        assignEqual(solver, handle);
        return;
    }

    if (ordering > 0)
        std::swap(aRW, bRW);

    VERIFY(!aRW.empty()); // because aRW != bRW and aRW.size >= bRW.size
    if (bRW.empty() && std::ranges::any_of(aRW, [](Member m) { return m.literal(); })) {
        assignDisequal(solver, handle);
        return;
    }
    if (aRW.front().literal() && bRW.front().literal()) {
        VERIFY(aRW.front() != bRW.front());
        assignDisequal(solver, handle);
        return;
    }
    if (aRW.back().literal() && bRW.back().literal()) {
        VERIFY(aRW.back() != bRW.back());
        assignDisequal(solver, handle);
        return;
    }

    if (solver.assignedTrue(equality)) {
        if (bRW.size() == 0) {
            // Rewrite all elements of a to the identity
            addIdentityRewrite(std::move(aRW), handle);
        } else if (auto subRange = std::ranges::search(aRW, bRW); !subRange.empty()) {
            aRW.erase(subRange.begin(), subRange.end());
            addIdentityRewrite(std::move(aRW), handle);
        } else if (bRW.size() == 1 && !bRW[0].literal()) {
            // Rewrite b[0] to a
            addRewrite(bRW[0], handle, std::move(aRW));
        } else {
            // Unassigned
        }
    }
}

void Members::addUses(Member m, PairHandle pair) {
    if (m.literal())
        return;
    if (m.composite()) {
        for (Member mp : compositeMember(m)) {
            if (!mp.literal())
                infoFor(mp).pairUses.push_back(pair);
        }
    } else {
        infoFor(m).pairUses.push_back(pair);
    }
}

void Members::newPair(Solver& solver, PairHandle pair) {
    VERIFY(dirtyRewrites.empty() && dirtyPairs.empty());
    auto [a, b] = solver.at(pair);
    addUses((Member)a, pair);
    addUses((Member)b, pair);

    updatePair(solver, pair);
    grind(solver);
}

void Members::propagateEqual(Solver& solver, PairHandle pair) {
    println("propagateEqual {}", pair.pairId());
    VERIFY(dirtyRewrites.empty() && dirtyPairs.empty());
    VERIFY(solver.assignedTrue(makeEquality(pair)));
    updatePair(solver, pair);
    grind(solver);
}

void Members::updateRewrites() {
    while (!dirtyRewrites.empty()) {
        RewriteTracePosition rwPos = dirtyRewrites.top();
        do {
            dirtyRewrites.pop();
        } while (!dirtyRewrites.empty() && dirtyRewrites.top() == rwPos);
        // Only non-identity rewrites can be dirty and always have a single target
        VERIFY(rewriteTrace[rwPos.index].targets.size() == 1);
        updateRewrite(infoFor(rewriteTrace[rwPos.index].targets.front()));
    }
}

void Members::grind(Solver& solver) {
    for (;;) {
        updateRewrites();

        if (dirtyPairs.empty())
            return;

        // TODO: The processing order here can affect which rewrites are selected
        //       so it may be worth to use a more stable ordering based on the rewrite order.
        PairHandle handle = dirtyPairs.top();
        do {
            dirtyPairs.pop();
        } while (!dirtyPairs.empty() && dirtyPairs.top() == handle);
        updatePair(solver, handle);
    }
}

bool Members::testReason(Solver&, BooleanValue assignedLiteral, const Reason& reason) {
    auto& pairInfo = pairs[assignedLiteral];
    if (reason.kind() == ReasonKind::MemberEquality) {
        return pairInfo.equality.has_value();
    } else if (reason.kind() == ReasonKind::MemberDisequality) {
        println("test disequal {} -> {}", pairOf(assignedLiteral).pairId(), pairInfo.disequality.has_value());
        return pairInfo.disequality.has_value();
    } else
        VERIFY_NOT_REACHED();
}

void Members::explainRewrite(Solver& solver, Member m, ClauseBuilder& clause) {
    println("explain {}:{}", nameString(m.theory()), m.id());
    if (m.literal())
        return;
    if (m.composite()) {
        for (Member mp : compositeMember(m))
            explainRewrite(solver, mp, clause);
        return;
    }

    auto& varInfo = infoFor(m);
    if (varInfo.tracePos.has_value()) {
        auto& entry = rewriteTrace[varInfo.tracePos->index];
        if (clause.add(solver, !makeEquality(entry.rewritePair))) {
            Pair pair = solver.at(entry.rewritePair);
            explainRewrite(solver, (Member)pair.source, clause);
            explainRewrite(solver, (Member)pair.target, clause);
        }
    }
}

ClauseAndIndex Members::reasonToClause(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) {
    // TODO: This likely doesn't work correctly with out of order reverted rewrites
    PairHandle pair = pairOf(assignedLiteral);
    auto [a, b] = solver.at(pair);
    ClauseBuilder clause = solver.beginClause();
    if (reason.kind() == ReasonKind::MemberEquality)
        VERIFY(!assignedLiteral.negated());
    else if (reason.kind() == ReasonKind::MemberDisequality)
        VERIFY(assignedLiteral.negated());
    else
        VERIFY_NOT_REACHED();
    clause.add(solver, assignedLiteral);

    explainRewrite(solver, (Member)a, clause);
    explainRewrite(solver, (Member)b, clause);
    return { solver.viewClause(clause), 0 };
}

void Members::newDecisionLevel(Solver& solver) {
    rewriteDecisionPoints.push_back(rewriteTrace.size());
    VERIFY((int_t)rewriteDecisionPoints.size() == solver.currentDecisionLevel() + 1);
    assignedPairDecisionPoints.push_back(assignedPairTrace.size());
    VERIFY((int_t)assignedPairDecisionPoints.size() == solver.currentDecisionLevel() + 1);
}

void Members::beginBacktrack(Solver& solver) {
    VERIFY(dirtyRewrites.empty() && dirtyPairs.empty());
    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
    int_t rwTargetSize = rewriteDecisionPoints[lastLevelToRevert];
    int_t eqTargetSize = assignedPairDecisionPoints[lastLevelToRevert];

    for (int_t i = rewriteTrace.size() - 1; i >= rwTargetSize; i--) {
        auto& [targets, rwPair] = rewriteTrace[i];
        for (Member target : targets) {
            auto& varInfo = infoFor(target);
            VERIFY(varInfo.tracePos == RewriteTracePosition(i));
            varInfo.currentRewrite = { target };
            for (Member m : varInfo.rewriteExpression) {
                if (!m.literal()) {
                    auto& otherVarInfo = infoFor(m);
                    VERIFY(otherVarInfo.rewriteUses.back() == RewriteTracePosition(i));
                    otherVarInfo.rewriteUses.pop_back();
                }
            }
            markUsesAsDirty(varInfo);
        }
        auto& pairInfo = pairs[makeEquality(rwPair)];
        VERIFY(pairInfo.rewrite == RewriteTracePosition(i));
        pairInfo.rewrite.reset();
    }

    for (int_t i = eqTargetSize; i < (int_t)assignedPairTrace.size(); i++) {
        auto& pairInfo = pairs[encodePairTheoryValue<TheoryId::MemberEquality>(assignedPairTrace[i])];
        VERIFY(pairInfo.equality == AssignedPairTracePosition(i) || pairInfo.disequality == AssignedPairTracePosition(i));
        pairInfo.equality.reset();
        pairInfo.disequality.reset();
    }
    assignedPairTrace.erase(assignedPairTrace.begin() + eqTargetSize, assignedPairTrace.end());
    assignedPairDecisionPoints.resize(lastLevelToRevert);

    while (!dirtyRewrites.empty() && (int_t)dirtyRewrites.top().index >= rwTargetSize) {
        dirtyRewrites.pop();
    }
    updateRewrites();
    decltype(dirtyPairs) emptyDirtyPairs;
    dirtyPairs = emptyDirtyPairs; // priority_queue has no clear()
}

void Members::endBacktrack(Solver& solver) {
    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
    int_t rwTargetSize = rewriteDecisionPoints[lastLevelToRevert];
    for (int_t i = rwTargetSize; i < (int_t)rewriteTrace.size(); i++) {
        for (Member target : rewriteTrace[i].targets) {
            infoFor(target).tracePos.reset();
        }
    }
    rewriteTrace.erase(rewriteTrace.begin() + rwTargetSize, rewriteTrace.end());
    rewriteDecisionPoints.resize(lastLevelToRevert);
}

}