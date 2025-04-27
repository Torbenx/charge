#include <check/StandardEquality.h>

#include <check/SatSolver.h>

#include <gtest/gtest.h>

namespace check {

StandardEquality::StandardEquality(Solver& solver, uint64_t baseLabel)
    : EqualityTheory(solver, baseLabel), ReasonTheory(solver, true) { }

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

void StandardEquality::applyEqual(Solver& solver, int_t eqId, bool propagate) {
    auto [source, target] = equalityLink(eqId);
    if (connected(solver, source, target))
        return;

    const auto& sourceInfo = infoFor(solver, source);
    auto& sourceRootInfo = infoFor(solver, sourceInfo.root);
    auto& sourceTree = sourceRootInfo.tree;
    auto& sourceEdges = sourceRootInfo.edges;
    auto& sourceDiseq = sourceRootInfo.disequalities;

    const auto& targetInfo = infoFor(solver, target);
    auto& targetRootInfo = infoFor(solver, targetInfo.root); // Careful: targetRootInfo and targetInfo may alias
    const auto& targetTree = targetRootInfo.tree;
    const auto& targetEdges = targetRootInfo.edges;
    const auto& targetDiseq = targetRootInfo.disequalities;

    if (propagate) {
        // Detect when after this link
        // a) both sides of an edge will belong to the same tree, or
        // b) the sides of an edge will belong to different sides of a disquality
        for (auto edge : sourceEdges) {
            if (edge.eqId == eqId)
                continue;
            Value otherRoot = infoFor(solver, edge.otherValue).root;
            if (otherRoot == targetInfo.root)
                assignEqual(solver, edge.eqId);

            forEachCommonElement(infoFor(solver, otherRoot).disequalities, targetDiseq, [this, &solver, &edge](int_t diseqId) {
                assignDisequal(solver, edge.eqId, diseqId);
            });
        }
        for (auto edge : targetEdges) {
            if (edge.eqId == eqId)
                continue;
            Value otherRoot = infoFor(solver, edge.otherValue).root;
            const auto& otherRootInfo = infoFor(solver, otherRoot);
            // Note: Similiar code in newVariable()
            if (isUnitDisequal(solver, sourceInfo.root, otherRoot)) {
                unitAssignDisequal(solver, edge.eqId, sourceInfo.root, otherRoot);
            } else {
                forEachCommonElement(otherRootInfo.disequalities, sourceDiseq, [this, &solver, &edge](int_t diseqId) {
                    assignDisequal(solver, edge.eqId, diseqId);
                });
            }
        }
    }
    mergeInto(sourceDiseq, targetDiseq);

    equalityTrace.push_back({ Link { source, target }, Link { sourceRootInfo.root, targetRootInfo.root } });

    int_t oldSourceTreeSize = sourceTree.size();
    sourceTree.push_back({ targetInfo.root, (uint32_t)(targetTree.size() + 1), (uint32_t)(sourceInfo.treeOffset + 1), (uint32_t)(targetInfo.treeOffset + 1) });
    sourceTree.insert(sourceTree.end(), targetTree.begin(), targetTree.end());

    int_t oldSourceEdgeCount = sourceEdges.size();
    sourceEdges.insert(sourceEdges.end(), targetEdges.begin(), targetEdges.end());

    targetRootInfo.root = sourceInfo.root;
    targetRootInfo.treeOffset = oldSourceTreeSize;
    targetRootInfo.edgesOffset += oldSourceEdgeCount;
    for (int_t i = 0; i < (int_t)targetTree.size(); i++) {
        auto& info = infoFor(solver, targetTree[i].value);
        info.root = sourceInfo.root;
        info.treeOffset = oldSourceTreeSize + 1 + i;
        info.edgesOffset += oldSourceEdgeCount;
    }
}

void StandardEquality::applyDisequal(Solver& solver, int_t diseqId, bool propagate) {
    auto [source, target] = equalityLink(diseqId);
    Value sourceRoot = infoFor(solver, source).root;
    Value targetRoot = infoFor(solver, target).root;
    if (shareAny(infoFor(solver, sourceRoot).disequalities, infoFor(solver, targetRoot).disequalities))
        return;
    if (isUnitDisequal(solver, sourceRoot, targetRoot))
        return;

    auto addDisequality = [&solver, diseqId](Value parent) {
        auto& disequalities = infoFor(solver, parent).disequalities;
        auto it = std::lower_bound(disequalities.begin(), disequalities.end(), diseqId);
        disequalities.insert(it, diseqId);
    };
    forEachParentOf(solver, source, addDisequality);
    forEachParentOf(solver, target, addDisequality);

    disequalityTrace.push_back({ (uint32_t)diseqId });

    if (propagate) {
        Value root = infoFor(solver, target).root;
        const auto& rootInfo = infoFor(solver, root);
        for (auto edge : rootInfo.edges) {
            if (edge.eqId == diseqId)
                continue;
            Value otherRoot = infoFor(solver, edge.otherValue).root;
            if (otherRoot != root && contains(infoFor(solver, otherRoot).disequalities, diseqId))
                assignDisequal(solver, edge.eqId, diseqId);
        }
    }
}

void StandardEquality::assignEqual(Solver& solver, int_t eqId) {
    solver.assignTrue(positiveLiteral(eqId), equalityReason());
}

void StandardEquality::assignDisequal(Solver& solver, int_t eqId, int_t diseqId) {
    bool normalConnectivity = connected(solver, equalityLink(eqId).source, equalityLink(diseqId).source);

    solver.assignTrue(negativeLiteral(eqId), disequalityReason(!normalConnectivity, diseqId));
}

void StandardEquality::unitAssignDisequal(Solver& solver, int_t eqId, Value unitDiseqA, Value unitDiseqB) {
    if (!connected(solver, equalityLink(eqId).source, unitDiseqA))
        std::swap(unitDiseqA, unitDiseqB);

    solver.assignTrue(negativeLiteral(eqId), unitDisequalityReason(unitDiseqA, unitDiseqB));
}

void StandardEquality::propagateAssignment(Solver& solver, BooleanValue lit) {
    if (isPositive(lit)) {
        applyEqual(solver, variableId(lit), true);
    } else {
        applyDisequal(solver, variableId(lit), true);
    }
}

void StandardEquality::reapplyAssignment(Solver& solver, BooleanValue lit) {
    if (isPositive(lit)) {
        applyEqual(solver, variableId(lit), false);
    } else {
        applyDisequal(solver, variableId(lit), false);
    }
}

void StandardEquality::unapplyAssignment(Solver&, BooleanValue) { }

bool StandardEquality::isUnitDisequalityReason(const Reason& reason) const { return reason.data0 == 2u; }
int_t StandardEquality::reasonDiseqId(const Reason& reason) const { return reason.data1; }
std::pair<Value, Value> StandardEquality::reasonDiseqOriented(const Reason& reason) {
    if (isUnitDisequalityReason(reason))
        return { std::bit_cast<Value>(reason.data1), std::bit_cast<Value>(reason.data2) };

    auto [a, b] = equalityLink(reasonDiseqId(reason));
    if (reason.data0 != 0)
        std::swap(a, b);
    return { a, b };
}
Reason StandardEquality::equalityReason() const {
    return Reason { (uint32_t)ReasonTheory::theoryId() };
}
Reason StandardEquality::disequalityReason(bool swappedConnectivity, int_t diseqId) const {
    return Reason {
        .reasonTheory = (uint32_t)ReasonTheory::theoryId(),
        .data0 = swappedConnectivity ? 1u : 0u,
        .data1 = (uint32_t)diseqId,
    };
}
Reason StandardEquality::unitDisequalityReason(Value diseqA, Value diseqB) {
    return Reason {
        .reasonTheory = (uint32_t)ReasonTheory::theoryId(),
        .data0 = 2u,
        .data1 = std::bit_cast<uint32_t>(diseqA),
        .data2 = std::bit_cast<uint32_t>(diseqB),
    };
}

bool StandardEquality::testReason(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) {
    if (isPositive(assignedLiteral)) {
        auto [source, target] = equalityLink(variableId(assignedLiteral));
        return connected(solver, source, target);
    }

    // disequality
    if (!isUnitDisequalityReason(reason) && !assignedNegative(solver, reasonDiseqId(reason)))
        return false;

    auto [impliedA, impliedB] = equalityLink(variableId(assignedLiteral));
    auto [originalA, originalB] = reasonDiseqOriented(reason);
    return connected(solver, impliedA, originalA) && connected(solver, impliedB, originalB);
}

int_t StandardEquality::newVariable(Solver& solver) {
    int_t eqId = SimpleBooleanTheory::newVariable(solver);

    auto [source, target] = equalityLink(eqId);
    addEdge(solver, source, target, eqId);
    addEdge(solver, target, source, eqId);

    const auto& sourceInfo = infoFor(solver, source);
    const auto& targetInfo = infoFor(solver, target);
    if (sourceInfo.root == targetInfo.root) {
        assignEqual(solver, eqId);
    } else {
        const auto& sourceRootInfo = infoFor(solver, sourceInfo.root);
        const auto& targetRootInfo = infoFor(solver, targetInfo.root);
        if (isUnitDisequal(solver, sourceInfo.root, targetInfo.root)) {
            unitAssignDisequal(solver, eqId, sourceInfo.root, targetInfo.root);
        } else {
            forEachCommonElement(sourceRootInfo.disequalities, targetRootInfo.disequalities, [this, &solver, eqId](int_t diseqId) {
                assignDisequal(solver, eqId, diseqId);
            });
        }
    }

    return eqId;
}

void StandardEquality::addEdge(Solver& solver, Value value, Value otherValue, int_t eqId) {
    const auto& valueInfo = infoFor(solver, value);
    int_t valueIndex = valueInfo.treeOffset;
    int_t edgeInsertPos = valueInfo.edgesOffset;
    EqualityInfo::Edge edge { .otherValue = otherValue, .eqId = (uint32_t)eqId };

    forEachParentOf(solver, value, [&solver, edgeInsertPos, edge](Value parent) {
        auto& info = infoFor(solver, parent);
        info.edges.insert(info.edges.begin() + (edgeInsertPos - info.edgesOffset), edge);
    });

    // Update indices after valueIndex
    // This works because the node array is naturally sorted by infoFor(solver, node.value).edgeOffset
    const auto& tree = infoFor(solver, valueInfo.root).tree;
    for (int_t index = valueIndex + 1; index < (int_t)tree.size(); index++)
        infoFor(solver, tree[index].value).edgesOffset += 1;
}

void StandardEquality::pathInTree(Solver& solver, Value a, Value b, std::vector<BooleanValue>& result) {
    VERIFY(connected(solver, a, b));

    struct StackEntry {
        int_t rootIndex;
        int_t aIndex;
        int_t bIndex;
    };
    std::vector<StackEntry> stack;

    Value treeRoot = infoFor(solver, a).root;
    const auto* tree = infoFor(solver, treeRoot).tree.data();
    int_t rootIndex = -1;
    int_t aIndex = infoFor(solver, a).treeOffset;
    int_t bIndex = infoFor(solver, b).treeOffset;
    for (;;) {
        if (aIndex == bIndex) {
            if (stack.empty())
                break;
            rootIndex = stack.back().rootIndex;
            aIndex = stack.back().aIndex;
            bIndex = stack.back().bIndex;
            stack.pop_back();
            continue;
        }
        if (aIndex > bIndex)
            std::swap(aIndex, bIndex);

        int_t index = rootIndex + 1;
        for (;;) {
            int_t nextIndex = index + tree[index].subTreeSize;
            if (nextIndex <= bIndex) {
                index = nextIndex;
                continue;
            }
            if (index < aIndex) {
                // The path between a and b entirely lies in this subtree.
                rootIndex = index;
                index = rootIndex + 1;
                continue;
            }
            break;
        }
        int_t sIndex = rootIndex + tree[index].linkSource;
        int_t tIndex = index + tree[index].linkTarget;
        result.push_back(negativeLiteral(lookupEqualityVariable(solver, sIndex == -1 ? treeRoot : tree[sIndex].value, tree[tIndex].value)));
        stack.push_back({ index, tIndex, bIndex });
        bIndex = sIndex;
    }
}

void StandardEquality::path(Solver& solver, Value a, Value b, std::vector<BooleanValue>& result) {
    Value aRoot = infoFor(solver, a).root;
    Value bRoot = infoFor(solver, b).root;
    if (aRoot == bRoot) {
        pathInTree(solver, a, b, result);
        return;
    }

    auto backtrackPos = [this, &solver](Value val) -> int_t {
        if (infoFor(solver, val).backtrackCounter == backtrackCounter)
            return infoFor(solver, val).backtrackTracePosition;
        return std::numeric_limits<int_t>::max();
    };

    int_t aIndex = backtrackPos(aRoot);
    int_t bIndex = backtrackPos(bRoot);
    VERIFY(aIndex != bIndex);

    for (;;) {
        if (aIndex > bIndex) {
            std::swap(aIndex, bIndex);
            std::swap(a, b);
        }

        const auto& entry = backtrackTrace[aIndex];
        aIndex = backtrackPos(entry.roots.source);
        if (aIndex == bIndex) {
            result.push_back(negativeLiteral(lookupEqualityVariable(solver, entry.link.source, entry.link.target)));
            path(solver, entry.link.target, a, result);
            path(solver, entry.link.source, b, result);
            return;
        }
    }
}

ReasonTheory::ClauseAndIndex StandardEquality::reasonToClause(Solver& solver, BooleanValue assignedLiteral, const Reason& reason) {
    auto& result = solver.scratchClause();

    if (isPositive(assignedLiteral)) {
        result.push_back(assignedLiteral);

        auto [a, b] = equalityLink(variableId(assignedLiteral));
        path(solver, a, b, result);

        return { .clause = result, .forceLiteralIndex = 0 };
    }

    result.push_back(assignedLiteral);
    if (!isUnitDisequalityReason(reason))
        result.push_back(positiveLiteral(reasonDiseqId(reason)));

    auto [impliedA, impliedB] = equalityLink(variableId(assignedLiteral));
    auto [originalA, originalB] = reasonDiseqOriented(reason);
    path(solver, impliedA, originalA, result);
    path(solver, impliedB, originalB, result);

    return { .clause = result, .forceLiteralIndex = 0 };
}

void StandardEquality::newDecisionLevel(Solver& solver) {
    equalityDecisionPoints.push_back(equalityTrace.size());
    disequalityDecisionPoints.push_back(disequalityTrace.size());
    VERIFY((int_t)equalityDecisionPoints.size() == solver.currentDecisionLevel() + 1);
    VERIFY((int_t)disequalityDecisionPoints.size() == solver.currentDecisionLevel() + 1);
}

void StandardEquality::backtrack(Solver& solver) {
    backtrackCounter += 1;
    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
    int_t targetSize = equalityDecisionPoints[lastLevelToRevert];
    backtrackTrace.assign(equalityTrace.begin() + targetSize, equalityTrace.end());

    while ((int_t)equalityTrace.size() > targetSize) {
        auto [link, roots] = equalityTrace.back();
        equalityTrace.pop_back();
        int_t backtrackTracePos = equalityTrace.size() - targetSize;

        auto& sourceRootInfo = infoFor(solver, roots.source);
        auto& targetRootInfo = infoFor(solver, roots.target);
        VERIFY(targetRootInfo.root == roots.source);
        sourceRootInfo.tree.resize(targetRootInfo.treeOffset);
        sourceRootInfo.edges.resize(targetRootInfo.edgesOffset);
        unmergeFrom(sourceRootInfo.disequalities, targetRootInfo.disequalities);
        int_t newEdgesSize = sourceRootInfo.edges.size();

        targetRootInfo.root = roots.target;
        targetRootInfo.treeOffset = -1;
        targetRootInfo.edgesOffset = 0;
        targetRootInfo.backtrackCounter = backtrackCounter;
        targetRootInfo.backtrackTracePosition = backtrackTracePos;
        for (int_t i = 0; i < (int_t)targetRootInfo.tree.size(); i++) {
            auto& info = infoFor(solver, targetRootInfo.tree[i].value);
            info.root = roots.target;
            info.treeOffset = i;
            info.edgesOffset -= newEdgesSize;
        }
    }
    equalityDecisionPoints.resize(lastLevelToRevert);

    targetSize = disequalityDecisionPoints[lastLevelToRevert];
    while ((int_t)disequalityTrace.size() > targetSize) {
        auto [diseqId] = disequalityTrace.back();
        disequalityTrace.pop_back();
        auto [source, target] = equalityLink(diseqId);

        auto removeDisequality = [&solver, diseqId](Value parent) {
            auto& disequalities = infoFor(solver, parent).disequalities;
            auto it = std::lower_bound(disequalities.begin(), disequalities.end(), diseqId);
            VERIFY(it != disequalities.end());
            VERIFY(*it == diseqId);
            disequalities.erase(it);
        };
        forEachParentOf(solver, source, removeDisequality);
        forEachParentOf(solver, target, removeDisequality);
    }
    disequalityDecisionPoints.resize(lastLevelToRevert);
}

void StandardEquality::checkInvariances(Solver& solver) {
    auto checkValue = [&solver](Value value) {
        const auto& info = infoFor(solver, value);
        const auto& rootInfo = infoFor(solver, info.root);
        if (info.treeOffset == -1) {
            VERIFY(info.root == value);
        } else {
            VERIFY(rootInfo.tree[info.treeOffset].value == value);
            VERIFY(rootInfo.tree[info.treeOffset].subTreeSize == info.tree.size() + 1);
        }
        VERIFY(std::equal(info.tree.begin(), info.tree.end(), rootInfo.tree.begin() + info.treeOffset + 1));
        VERIFY(std::equal(info.edges.begin(), info.edges.end(), rootInfo.edges.begin() + info.edgesOffset));
        VERIFY(commonElements(info.disequalities, rootInfo.disequalities) == info.disequalities);

        if (info.root == value) {
            for (int_t i = 1; i < (int_t)info.tree.size(); i++) {
                VERIFY(infoFor(solver, info.tree[i - 1].value).edgesOffset <= infoFor(solver, info.tree[i].value).edgesOffset);
            }
        }
    };
    for (int_t eqId = 0; eqId < variableCount(); eqId++) {
        auto [source, target] = equalityLink(eqId);
        checkValue(source);
        checkValue(target);
    }
}

}