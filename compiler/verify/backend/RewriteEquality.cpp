#include <verify/backend/RewriteEquality.h>

#include <gtest/gtest.h>

namespace verify::backend {

namespace {
    using flat_set = std::vector<uint32_t>;
    void mergeInto(flat_set& a, const flat_set& b) {
        a.resize(a.size() + b.size());
        auto insert = a.rbegin();
        auto aIt = a.rbegin() + b.size();
        auto bIt = b.rbegin();
        for (;;) {
            if (aIt == a.rend()) {
                std::copy(bIt, b.rend(), insert);
                return;
            }
            if (bIt == b.rend()) {
                VERIFY(insert == aIt);
                return;
            }

            if (*aIt > *bIt) {
                *insert = *aIt;
                ++aIt;
            } else {
                *insert = *bIt;
                ++bIt;
            }
            ++insert;
        }
    }

    void unmergeFrom(flat_set& a, const flat_set& b) {
        auto insert = a.begin();
        auto aIt = a.begin();
        auto bIt = b.begin();
        for (;;) {
            if (bIt == b.end()) {
                if (aIt != insert) {
                    insert = std::copy(aIt, a.end(), insert);
                    a.erase(insert, a.end());
                }
                return;
            }

            if (*aIt < *bIt) {
                *insert = *aIt;
                ++insert;
                ++aIt;
            } else if (*aIt == *bIt) {
                ++aIt;
                ++bIt;
            } else {
                ++bIt;
            }
        }
    }

    void forEachCommonElement(const flat_set& a, const flat_set& b, auto&& callback) {
        auto aIt = a.begin();
        auto bIt = b.begin();
        for (;;) {
            if (aIt == a.end() || bIt == b.end())
                return;

            if (*aIt < *bIt) {
                ++aIt;
            } else if (*aIt == *bIt) {
                callback(*aIt);
                ++aIt;
                ++bIt;
            } else {
                ++bIt;
            }
        }
    }

    flat_set commonElements(const flat_set& a, const flat_set& b) {
        flat_set result;
        forEachCommonElement(a, b, [&result](uint32_t x) { result.push_back(x); });
        return result;
    }

    bool contains(const flat_set& a, uint32_t val) {
        return std::lower_bound(a.begin(), a.end(), val) != a.end();
    }

    bool shareAny(const flat_set& a, const flat_set& b) {
        auto aIt = a.begin();
        auto bIt = b.begin();
        for (;;) {
            if (aIt == a.end() || bIt == b.end())
                return false;

            if (*aIt < *bIt) {
                ++aIt;
            } else if (*aIt == *bIt) {
                return true;
            } else {
                ++bIt;
            }
        }
    }
}

TEST(Check, MergeLists) {
    auto mergeUnmerge = [](flat_set orgA, flat_set b) {
        auto a = orgA;
        mergeInto(a, b);
        std::vector<uint32_t> stdMerged;
        stdMerged.resize(orgA.size() + b.size());
        std::merge(orgA.begin(), orgA.end(), b.begin(), b.end(), stdMerged.begin());
        EXPECT_EQ(a, stdMerged);

        EXPECT_EQ(commonElements(a, b), b);

        unmergeFrom(a, b);
        EXPECT_EQ(a, orgA);
    };

    mergeUnmerge({ 0, 2, 4, 6 }, {});
    mergeUnmerge({}, { 0, 2, 4, 6 });
    mergeUnmerge({ 0, 2, 4, 6 }, { 1, 3, 5, 7 });
    mergeUnmerge({ 1, 3, 5, 7 }, { 0, 2, 4, 6 });
    mergeUnmerge({ 1, 2, 3, 7, 8, 9 }, { 4, 5, 6 });
}

struct DisequalPair {
    Value a;
    Value b;
};

struct DisequalityReason : private PackedReason<DisequalPair, uint32_t> {
    DisequalityReason(uint32_t pairId, Value a, Value b)
        : PackedReason({ a, b }, pairId) { }

    uint32_t pairId() const { return tag(); }
    DisequalPair diseqPair() const { return data(); }
};

RewriteEquality::RewriteEquality(Solver& solver, ValueKind valueKind, TheoryId theory)
    : equalityInfos(solver, valueKind), m_theory(theory), m_valueKind(valueKind) { }

void RewriteEquality::propagateEqual(Solver& solver, PairHandle eqPair) {
    auto [source, target] = solver.at(eqPair);
    if (connected(source, target))
        return;

    const auto& sourceInfo = infoFor(source);
    auto& sourceRootInfo = infoFor(sourceInfo.root);
    auto& sourceTree = sourceRootInfo.tree;
    auto& sourceEdges = sourceRootInfo.edges;
    auto& sourceDiseq = sourceRootInfo.disequalities;

    const auto& targetInfo = infoFor(target);
    auto& targetRootInfo = infoFor(targetInfo.root); // Careful: targetRootInfo and targetInfo may alias
    const auto& targetTree = targetRootInfo.tree;
    const auto& targetEdges = targetRootInfo.edges;
    const auto& targetDiseq = targetRootInfo.disequalities;

    // Detect when after this link
    // a) both sides of an edge will belong to the same tree, or
    // b) the sides of an edge will belong to different sides of a disquality
    for (auto edge : sourceEdges) {
        if (edge.pair == eqPair)
            continue;
        Value otherRoot = infoFor(edge.otherValue).root;
        if (otherRoot == targetInfo.root)
            assignEqual(solver, edge.pair);

        forEachCommonElement(infoFor(otherRoot).disequalities, targetDiseq, [this, &solver, &edge](int_t diseqId) {
            assignDisequal(solver, edge.pair, PairHandle(m_valueKind, diseqId));
        });
    }
    for (auto edge : targetEdges) {
        if (edge.pair == eqPair)
            continue;
        Value otherRoot = infoFor(edge.otherValue).root;
        const auto& otherRootInfo = infoFor(otherRoot);
        // Note: Similiar code in newVariable()
        if (solver.alwaysDisequal(sourceInfo.root, otherRoot)) {
            assignDisequalByAlwaysDisequal(solver, edge.pair, sourceInfo.root, otherRoot);
        } else {
            forEachCommonElement(otherRootInfo.disequalities, sourceDiseq, [this, &solver, &edge](int_t diseqId) {
                assignDisequal(solver, edge.pair, PairHandle(m_valueKind, diseqId));
            });
        }
    }
    mergeInto(sourceDiseq, targetDiseq);

    TracePosition tracePosition { (uint32_t)equalityTrace.size() };
    equalityTrace.push_back({ Pair { source, target }, Pair { sourceRootInfo.root, targetRootInfo.root } });

    int_t oldSourceTreeSize = sourceTree.size();
    sourceTree.push_back({ targetInfo.root });
    sourceTree.insert(sourceTree.end(), targetTree.begin(), targetTree.end());

    int_t oldSourceEdgeCount = sourceEdges.size();
    sourceEdges.insert(sourceEdges.end(), targetEdges.begin(), targetEdges.end());

    targetRootInfo.root = sourceInfo.root;
    targetRootInfo.treeOffset = oldSourceTreeSize;
    targetRootInfo.edgesOffset += oldSourceEdgeCount;
    targetRootInfo.tracePosition = tracePosition;
    for (int_t i = 0; i < (int_t)targetTree.size(); i++) {
        auto& info = infoFor(targetTree[i].value);
        info.root = sourceInfo.root;
        info.treeOffset = oldSourceTreeSize + 1 + i;
        info.edgesOffset += oldSourceEdgeCount;
    }
}

void RewriteEquality::propagateDisequal(Solver& solver, PairHandle diseqPair) {
    auto [source, target] = solver.at(diseqPair);
    Value sourceRoot = infoFor(source).root;
    Value targetRoot = infoFor(target).root;
    if (shareAny(infoFor(sourceRoot).disequalities, infoFor(targetRoot).disequalities))
        return;
    if (solver.alwaysDisequal(sourceRoot, targetRoot))
        return;

    auto addDisequality = [this, diseqPair](Value parent) {
        auto& disequalities = infoFor(parent).disequalities;
        auto it = std::lower_bound(disequalities.begin(), disequalities.end(), diseqPair.pairId());
        disequalities.insert(it, diseqPair.pairId());
    };
    forEachParentOf(source, addDisequality);
    forEachParentOf(target, addDisequality);

    disequalityTrace.push_back({ diseqPair });

    Value root = infoFor(target).root;
    const auto& rootInfo = infoFor(root);
    for (auto edge : rootInfo.edges) {
        if (edge.pair == diseqPair)
            continue;
        Value otherRoot = infoFor(edge.otherValue).root;
        if (otherRoot != root && contains(infoFor(otherRoot).disequalities, diseqPair.pairId()))
            assignDisequal(solver, edge.pair, diseqPair);
    }
}

void RewriteEquality::assignEqual(Solver& solver, PairHandle assignPair) {
    solver.assignTrue(makeEquality(assignPair), makeReason<ReasonKind::Equality>({}));
}

void RewriteEquality::assignDisequal(Solver& solver, PairHandle assignPair, PairHandle diseqPair) {
    auto [diseqA, diseqB] = solver.at(diseqPair);
    if (!connected(solver.at(assignPair).source, diseqA))
        std::swap(diseqA, diseqB);

    solver.assignTrue(!makeEquality(assignPair), makeReason<ReasonKind::Disequality>({ diseqPair.pairId(), diseqA, diseqB }));
}

void RewriteEquality::assignDisequalByAlwaysDisequal(Solver& solver, PairHandle assignPair, Value alwaysDiseqA, Value alwaysDiseqB) {
    if (!connected(solver.at(assignPair).source, alwaysDiseqA))
        std::swap(alwaysDiseqA, alwaysDiseqB);

    solver.assignTrue(!makeEquality(assignPair), makeReason<ReasonKind::DisequalityByAlwaysDisequal>({ 0, alwaysDiseqA, alwaysDiseqB }));
}

void RewriteEquality::newPair(Solver& solver, PairHandle pair) {
    VERIFY(pair.valueKind() == m_valueKind);
    auto [source, target] = solver.at(pair);
    addEdge(source, target, pair);
    addEdge(target, source, pair);

    const auto& sourceInfo = infoFor(source);
    const auto& targetInfo = infoFor(target);
    if (sourceInfo.root == targetInfo.root) {
        assignEqual(solver, pair);
    } else {
        const auto& sourceRootInfo = infoFor(sourceInfo.root);
        const auto& targetRootInfo = infoFor(targetInfo.root);
        if (solver.alwaysDisequal(sourceInfo.root, targetInfo.root)) {
            assignDisequalByAlwaysDisequal(solver, pair, sourceInfo.root, targetInfo.root);
        } else {
            forEachCommonElement(sourceRootInfo.disequalities, targetRootInfo.disequalities, [this, &solver, pair](int_t diseqId) {
                assignDisequal(solver, pair, PairHandle(m_valueKind, diseqId));
            });
        }
    }
}

void RewriteEquality::addEdge(Value value, Value otherValue, PairHandle pair) {
    const auto& valueInfo = infoFor(value);
    int_t valueIndex = valueInfo.treeOffset;
    int_t edgeInsertPos = valueInfo.edgesOffset;
    EqualityInfo::Edge edge { .otherValue = otherValue, .pair = pair };

    forEachParentOf(value, [this, edgeInsertPos, edge](Value parent) {
        auto& info = infoFor(parent);
        info.edges.insert(info.edges.begin() + (edgeInsertPos - info.edgesOffset), edge);
    });

    // Update indices after valueIndex
    // This works because the node array is naturally sorted by infoFor(solver, node.value).edgeOffset
    const auto& tree = infoFor(valueInfo.root).tree;
    for (int_t index = valueIndex + 1; index < (int_t)tree.size(); index++)
        infoFor(tree[index].value).edgesOffset += 1;
}

void RewriteEquality::path(Solver& solver, Value a, Value b, ClauseBuilder& result) {
    if (a == b)
        return;

    auto tracePos = [this](Value val) -> int_t {
        return infoFor(val).tracePosition.value_or(TracePosition { std::numeric_limits<uint32_t>::max() }).index;
    };

    int_t aIndex = tracePos(a);
    int_t bIndex = tracePos(b);
    VERIFY(aIndex != bIndex);

    for (;;) {
        if (aIndex > bIndex) {
            std::swap(aIndex, bIndex);
            std::swap(a, b);
        }

        const auto& entry = equalityTrace[aIndex];
        aIndex = tracePos(entry.roots.source);
        if (aIndex == bIndex) {
            result.add(solver, !makeEquality(solver.findPair(entry.link)));
            path(solver, entry.link.target, a, result);
            path(solver, entry.link.source, b, result);
            return;
        }
    }
}

bool RewriteEquality::testReason(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) {
    if (reason.kind() == ReasonKind::Equality) {
        auto [source, target] = solver.at(pairOf(assignedLiteral));
        return connected(source, target);
    }

    // disequality
    DisequalityReason data = reason.getData<DisequalityReason>();
    if (reason.kind() == ReasonKind::Disequality && !solver.assignedFalse(makeEquality(PairHandle(m_valueKind, data.pairId()))))
        return false;

    auto [impliedA, impliedB] = solver.at(pairOf(assignedLiteral));
    auto [originalA, originalB] = data.diseqPair();
    return connected(impliedA, originalA) && connected(impliedB, originalB);
}

ClauseAndIndex RewriteEquality::reasonToClause(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) {
    auto result = solver.beginClause();

    if (reason.kind() == ReasonKind::Equality) {
        result.add(solver, assignedLiteral);

        auto [a, b] = solver.at(pairOf(assignedLiteral));
        path(solver, a, b, result);

        return { .clause = solver.viewClause(result), .forceLiteralIndex = 0 };
    }

    // disequality
    result.add(solver, assignedLiteral);
    auto data = reason.getData<DisequalityReason>();
    if (reason.kind() == ReasonKind::Disequality)
        result.add(solver, makeEquality(PairHandle(m_valueKind, data.pairId())));

    auto [impliedA, impliedB] = solver.at(pairOf(assignedLiteral));
    auto [originalA, originalB] = data.diseqPair();
    path(solver, impliedA, originalA, result);
    path(solver, impliedB, originalB, result);

    return { .clause = solver.viewClause(result), .forceLiteralIndex = 0 };
}

void RewriteEquality::newDecisionLevel(Solver& solver) {
    equalityDecisionPoints.push_back(equalityTrace.size());
    disequalityDecisionPoints.push_back(disequalityTrace.size());
    VERIFY((int_t)equalityDecisionPoints.size() == solver.currentDecisionLevel() + 1);
    VERIFY((int_t)disequalityDecisionPoints.size() == solver.currentDecisionLevel() + 1);
}

void RewriteEquality::beginBacktrack(Solver& solver) {
    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
    int_t eqTargetSize = equalityDecisionPoints[lastLevelToRevert];
    for (int_t i = equalityTrace.size() - 1; i >= eqTargetSize; i--) {
        auto [link, roots] = equalityTrace[i];

        auto& sourceRootInfo = infoFor(roots.source);
        auto& targetRootInfo = infoFor(roots.target);
        VERIFY(targetRootInfo.root == roots.source);
        sourceRootInfo.tree.erase(sourceRootInfo.tree.begin() + targetRootInfo.treeOffset, sourceRootInfo.tree.end());
        sourceRootInfo.edges.erase(sourceRootInfo.edges.begin() + targetRootInfo.edgesOffset, sourceRootInfo.edges.end());
        unmergeFrom(sourceRootInfo.disequalities, targetRootInfo.disequalities);
        int_t newEdgesSize = sourceRootInfo.edges.size();

        targetRootInfo.root = roots.target;
        targetRootInfo.treeOffset = -1;
        targetRootInfo.edgesOffset = 0;
        for (int_t i = 0; i < (int_t)targetRootInfo.tree.size(); i++) {
            auto& info = infoFor(targetRootInfo.tree[i].value);
            info.root = roots.target;
            info.treeOffset = i;
            info.edgesOffset -= newEdgesSize;
        }
    }

    int_t diseqTargetSize = disequalityDecisionPoints[lastLevelToRevert];
    while ((int_t)disequalityTrace.size() > diseqTargetSize) {
        auto [diseqPair] = disequalityTrace.back();
        disequalityTrace.pop_back();
        auto [source, target] = solver.at(diseqPair);

        auto removeDisequality = [this, diseqPair](Value parent) {
            auto& disequalities = infoFor(parent).disequalities;
            auto it = std::lower_bound(disequalities.begin(), disequalities.end(), diseqPair.pairId());
            VERIFY(it != disequalities.end());
            VERIFY(*it == diseqPair.pairId());
            disequalities.erase(it);
        };
        forEachParentOf(source, removeDisequality, eqTargetSize);
        forEachParentOf(target, removeDisequality, eqTargetSize);
    }
    disequalityDecisionPoints.resize(lastLevelToRevert);
}

void RewriteEquality::endBacktrack(Solver& solver) {
    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
    int_t targetSize = equalityDecisionPoints[lastLevelToRevert];
    for (int_t i = targetSize; i < (int_t)equalityTrace.size(); i++) {
        infoFor(equalityTrace[i].roots.target).tracePosition.reset();
    }
    equalityTrace.erase(equalityTrace.begin() + targetSize, equalityTrace.end());
    equalityDecisionPoints.resize(lastLevelToRevert);
}

void RewriteEquality::checkInvariances(Solver& solver) {
    auto checkValue = [this](Value value) {
        const auto& info = infoFor(value);
        const auto& rootInfo = infoFor(info.root);
        if (info.treeOffset == -1) {
            VERIFY(info.root == value);
            VERIFY(!info.tracePosition.has_value());
        } else {
            VERIFY(rootInfo.tree[info.treeOffset].value == value);
            VERIFY(value == equalityTrace[info.tracePosition->index].roots.target);
        }
        VERIFY(std::equal(info.tree.begin(), info.tree.end(), rootInfo.tree.begin() + info.treeOffset + 1));
        VERIFY(std::equal(info.edges.begin(), info.edges.end(), rootInfo.edges.begin() + info.edgesOffset));
        VERIFY(commonElements(info.disequalities, rootInfo.disequalities) == info.disequalities);

        if (info.root == value) {
            for (int_t i = 1; i < (int_t)info.tree.size(); i++) {
                VERIFY(infoFor(info.tree[i - 1].value).edgesOffset <= infoFor(info.tree[i].value).edgesOffset);
            }
        }
    };
    for (int_t eqId = 0; eqId < solver.booleanCount(m_theory); eqId++) {
        auto [source, target] = solver.at(PairHandle(m_valueKind, eqId));
        checkValue(source);
        checkValue(target);
    }
}

}