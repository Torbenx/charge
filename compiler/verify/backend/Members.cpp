#include <verify/backend/Members.h>

#include <verify/backend/SolverImpl.h>

#include <algorithm>
#include <queue>
#include <unordered_map>

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

void Members::appendRewrite(Member m, std::vector<Member>& out) {
    if (m.literal()) {
        out.push_back(m);
        return;
    }
    if (!m.composite()) {
        auto& varInfo = infoFor(m);
        out.insert(out.end(), varInfo.currentRewrite.begin(), varInfo.currentRewrite.end());
        return;
    }

    for (Member mp : compositeMember(m)) {
        if (mp.literal()) {
            out.push_back(mp);
        } else {
            auto& varInfo = infoFor(mp);
            out.insert(out.end(), varInfo.currentRewrite.begin(), varInfo.currentRewrite.end());
        }
    }
}

std::vector<Member> Members::rewrite(Member m) {
    std::vector<Member> result;
    appendRewrite(m, result);
    return result;
}

void Members::addUse(Solver&, Value value, Use use) {
    VERIFY(sortOf(value.theory()) == Sort::Member);
    Member expression = (Member)value;
    auto registerFor = [this, use](Member m) {
        infoFor(m).uses.push_back(use);
        useTrace.push_back({ m, use });
    };

    if (!expression.composite()) {
        if (!expression.literal())
            registerFor(expression);
        return;
    }

    auto letters = compositeMember(expression);
    for (int_t i = 0; i < (int_t)letters.size(); i++) {
        Member m = letters[i];
        if (m.literal())
            continue;
        // A variable may occur several times in the expression, but one registration is enough
        auto seen = letters.first(i);
        if (std::ranges::find(seen, m) != seen.end())
            continue;
        registerFor(m);
    }
}

void Members::sendRewrites(Solver& solver) {
    VERIFY(dirtyRewrites.empty() && dirtyPairs.empty());

    std::vector<Use> uses;
    for (Member m : externalPropagationQueue) {
        auto& varInfo = infoFor(m);
        VERIFY(varInfo.queuedForExternalPropagation);
        varInfo.queuedForExternalPropagation = false;
        uses.insert(uses.end(), varInfo.uses.begin(), varInfo.uses.end());
    }
    externalPropagationQueue.clear();

    // A use is added once per variable in the target expression. If multiple variables
    // change in the same update we can end up with duplicates here.
    std::ranges::sort(uses, {}, [](Use use) { return std::bit_cast<uint32_t>(use); });
    uses.erase(std::ranges::unique(uses).begin(), uses.end());

    // Note: A notification may register new uses and apply further rewrites, which is why the
    //       change log is already emptied and the batch is held in a local.
    for (Use use : uses)
        solver.impl().propagateRewrite(use);
}

void Members::markUsesAsDirty(VariableInfo& varInfo, bool externalPropagation) {
    if (externalPropagation && !varInfo.queuedForExternalPropagation) {
        varInfo.queuedForExternalPropagation = true;
        externalPropagationQueue.push_back(varInfo.self);
    }

    for (RewriteTracePosition use : varInfo.rewriteUses) {
        VERIFY(!varInfo.tracePos.has_value() || use < varInfo.tracePos.value());
        dirtyRewrites.push(use);
    }
    for (PairHandle pair : varInfo.pairUses) {
        dirtyPairs.push(pair);
    }
}

void Members::addRewrite(Member target, PairHandle pair, std::vector<Member> expression) {
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
    markUsesAsDirty(varInfo, true);
}

void Members::addIdentityRewrite(std::vector<Member> targets, PairHandle pair) {
    RewriteTracePosition tracePos(rewriteTrace.size());
    pairs[makeEquality(pair)].rewrite = tracePos;
    for (Member target : targets) {
        auto& varInfo = infoFor(target);
        varInfo.currentRewrite.clear();
        varInfo.rewriteExpression.clear();
        varInfo.tracePos = tracePos;
        markUsesAsDirty(varInfo, true);
    }
    rewriteTrace.push_back({ std::move(targets), pair });
}

void Members::updateRewrite(VariableInfo& varInfo, bool externalPropagation) {
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
    markUsesAsDirty(varInfo, externalPropagation);
}

void Members::assignEqual(Solver& solver, PairHandle handle) {
    AssignedPairTracePosition tracePos(assignedPairTrace.size());
    assignedPairTrace.push_back(handle);
    auto equality = makeEquality(handle);
    pairs[equality].equality = tracePos;
    solver.assignTrue(equality, makeReason<ReasonKind::MemberEquality>({}));
}

void Members::assignDisequal(Solver& solver, PairHandle handle) {
    AssignedPairTracePosition tracePos(assignedPairTrace.size());
    assignedPairTrace.push_back(handle);
    auto equality = makeEquality(handle);
    pairs[equality].disequality = tracePos;
    solver.assignTrue(!equality, makeReason<ReasonKind::MemberDisequality>({}));
}

void Members::updatePair(Solver& solver, PairHandle handle) {
    VERIFY(dirtyRewrites.empty());
    Bool equality(encodePairTheoryValue<TheoryId::MemberEquality>(handle));
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
    if (auto subRange = std::ranges::search(aRW, bRW); subRange.size() == bRW.size()) {
        // b is a subexpression of a or b is empty
        aRW.erase(subRange.begin(), subRange.end());
        if (std::ranges::any_of(aRW, [](Member m) { return m.literal(); })) {
            // a contains a literal can thus never be empty
            assignDisequal(solver, handle);
        } else if (solver.assignedTrue(equality)) {
            // Rewrite all elements of a to the identity
            addIdentityRewrite(std::move(aRW), handle);
        }
        return;
    }

    if (aRW.front().literal() && bRW.front().literal()) {
        // a.front() and  b.front() are distinct literals
        VERIFY(aRW.front() != bRW.front());
        assignDisequal(solver, handle);
    } else if (aRW.back().literal() && bRW.back().literal()) {
        // a.back() and  b.back() are distinct literals
        VERIFY(aRW.back() != bRW.back());
        assignDisequal(solver, handle);
    } else if (solver.assignedTrue(equality) && bRW.size() == 1 && !bRW[0].literal()) {
        // Rewrite b[0] to a
        addRewrite(bRW[0], handle, std::move(aRW));
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
    VERIFY(dirtyRewrites.empty() && dirtyPairs.empty());
    VERIFY(solver.assignedTrue(makeEquality(pair)));
    updatePair(solver, pair);
    grind(solver);
}

void Members::updateRewrites(bool externalPropagation) {
    while (!dirtyRewrites.empty()) {
        RewriteTracePosition rwPos = dirtyRewrites.top();
        do {
            dirtyRewrites.pop();
        } while (!dirtyRewrites.empty() && dirtyRewrites.top() == rwPos);
        // Only non-identity rewrites can be dirty and always have a single target
        VERIFY(rewriteTrace[rwPos.index].targets.size() == 1);
        updateRewrite(infoFor(rewriteTrace[rwPos.index].targets.front()), externalPropagation);
    }
}

void Members::grind(Solver& solver) {
    for (;;) {
        updateRewrites(true);

        if (dirtyPairs.empty())
            break;

        // TODO: The processing order here can affect which rewrites are selected
        //       so it may be worth to use a more stable ordering based on the rewrite order.
        PairHandle handle = dirtyPairs.top();
        do {
            dirtyPairs.pop();
        } while (!dirtyPairs.empty() && dirtyPairs.top() == handle);
        updatePair(solver, handle);
    }
    sendRewrites(solver);
}

bool Members::testReason(Solver&, Bool assignedLiteral, const Reason& reason) {
    auto& pairInfo = pairs[assignedLiteral];
    if (reason.kind() == ReasonKind::MemberEquality) {
        return pairInfo.equality.has_value();
    } else if (reason.kind() == ReasonKind::MemberDisequality) {
        return pairInfo.disequality.has_value();
    } else
        VERIFY_NOT_REACHED();
}

void Members::explainRewrite(Solver& solver, Member m, ClauseBuilder& clause) {
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

ClauseAndIndex Members::reasonToClause(Solver& solver, Bool assignedLiteral, const Reason& reason) {
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
    useDecisionPoints.push_back(useTrace.size());
    VERIFY((int_t)useDecisionPoints.size() == solver.currentDecisionLevel() + 1);
}

void Members::beginBacktrack(Solver& solver) {
    VERIFY(dirtyRewrites.empty() && dirtyPairs.empty());
    // Everything that was applied has been notified about before the backtrack was started
    VERIFY(externalPropagationQueue.empty());
    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
    int_t rwTargetSize = rewriteDecisionPoints[lastLevelToRevert];
    int_t eqTargetSize = assignedPairDecisionPoints[lastLevelToRevert];

    // The uses were appended in registration order, so the ones of the reverted levels are the tail
    // of the list of their variable
    int_t useTargetSize = useDecisionPoints[lastLevelToRevert];
    while ((int_t)useTrace.size() > useTargetSize) {
        auto [variable, use] = useTrace.back();
        auto& uses = infoFor(variable).uses;
        VERIFY(!uses.empty());
        VERIFY(uses.back() == use);
        uses.pop_back();
        useTrace.pop_back();
    }
    useDecisionPoints.resize(lastLevelToRevert);

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
            markUsesAsDirty(varInfo, false);
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
    updateRewrites(false);
    // No new information should become available as result of the backtrack
    // so there is no need to update the pairs.
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

void Members::checkInvariances(Solver& solver) {
    // The theory is only quiescent while there is nothing left to update and every change was
    // notified about
    VERIFY(dirtyRewrites.empty() && dirtyPairs.empty());
    VERIFY(externalPropagationQueue.empty());

    // The entries of a level are appended after its decision point, so the points only grow
    auto checkDecisionPoints = [&solver](const std::vector<uint32_t>& points, size_t traceSize) {
        VERIFY((int_t)points.size() == solver.currentDecisionLevel() + 1);
        VERIFY(std::ranges::is_sorted(points));
        VERIFY(points.empty() || points.back() <= traceSize);
    };
    checkDecisionPoints(rewriteDecisionPoints, rewriteTrace.size());
    checkDecisionPoints(assignedPairDecisionPoints, assignedPairTrace.size());
    checkDecisionPoints(useDecisionPoints, useTrace.size());

    // The uses a variable is expected to have, in registration order because beginBacktrack() relies
    // on being able to pop them from the back
    std::unordered_map<uint32_t, std::vector<Use>> expectedUses;
    for (auto [variable, use] : useTrace)
        expectedUses[std::bit_cast<uint32_t>((Value)variable)].push_back(use);

    for (int_t theoryId = 0; theoryId < std::to_underlying(TheoryId::COUNT); theoryId++) {
        TheoryId theory = (TheoryId)theoryId;
        if (sortOf(theory) != Sort::Member)
            continue;

        solver.forEachValue(theory, [this, &expectedUses](Value v) {
            Member m(v);
            if (!m.variable())
                return;

            const VariableInfo& varInfo = infoFor(m);
            VERIFY(varInfo.self == m);
            // The flag only lives between a change and the notifications made for it
            VERIFY(!varInfo.queuedForExternalPropagation);

            // A variable expands to the expansion of its rewrite expression, or to itself as long
            // as it has no rewrite. So anything else here means that an update was missed.
            std::vector<Member> expansion;
            if (varInfo.hasRewrite()) {
                for (Member e : varInfo.rewriteExpression)
                    appendRewrite(e, expansion);
            } else {
                expansion.push_back(m);
            }
            VERIFY(varInfo.currentRewrite == expansion);

            // Every variable has exactly the uses registered for it, so no use of a reverted level
            // may have been left behind
            auto it = expectedUses.find(std::bit_cast<uint32_t>(v));
            std::span<const Use> expected;
            if (it != expectedUses.end())
                expected = it->second;
            VERIFY(std::ranges::equal(varInfo.uses, expected));
        });
    }
}

}