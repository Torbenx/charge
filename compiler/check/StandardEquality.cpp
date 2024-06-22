#include <check/StandardEquality.h>

#include <check/SatSolver.h>

namespace check {

StandardEquality::StandardEquality(Solver& solver)
    : EqualityTheory(solver), ReasonTheory(solver, true) { }

void StandardEquality::link(Solver& solver, Value source, Value target) {
    VERIFY(!connected(source, target));

    const auto& sourceInfo = equalityInfo(source);
    auto& sourceRootInfo = equalityInfo(sourceInfo.root);
    auto& sourceTree = sourceRootInfo.tree;
    auto& sourceWatches = sourceRootInfo.watches;

    const auto& targetInfo = equalityInfo(target);
    auto& targetRootInfo = equalityInfo(targetInfo.root); // Careful: targetRootInfo and targetInfo may alias
    const auto& targetTree = targetRootInfo.tree;
    const auto& targetWatches = targetRootInfo.watches;

    // Detect when both side of a watch will belong to the same tree after this assignment
    for (auto watch : sourceWatches) {
        if (equalityInfo(watch.otherValue).root == targetInfo.root)
            solver.assignTrue(positiveLiteral(watch.eqId), linkToReason(equalities.at(watch.eqId)));
    }
    for (auto watch : targetWatches) {
        if (watch.otherValue != source && equalityInfo(watch.otherValue).root == sourceInfo.root)
            solver.assignTrue(positiveLiteral(watch.eqId), linkToReason(equalities.at(watch.eqId)));
    }

    int_t oldSourceTreeSize = sourceTree.size();
    sourceTree.push_back({ targetInfo.root, (uint32_t)(targetTree.size() + 1), (uint32_t)(sourceInfo.treeOffset + 1), (uint32_t)(targetInfo.treeOffset + 1) });
    sourceTree.insert(sourceTree.end(), targetTree.begin(), targetTree.end());

    int_t oldSourceWatchCount = sourceWatches.size();
    sourceWatches.insert(sourceWatches.end(), targetWatches.begin(), targetWatches.end());

    targetRootInfo.root = sourceInfo.root;
    targetRootInfo.treeOffset = oldSourceTreeSize;
    targetRootInfo.watchOffset += oldSourceWatchCount;
    for (int_t i = 0; i < (int_t)targetTree.size(); i++) {
        auto& info = equalityInfo(targetTree[i].value);
        info.root = sourceInfo.root;
        info.treeOffset = oldSourceTreeSize + 1 + i;
        info.watchOffset += oldSourceWatchCount;
    }
    checkInvariances();
}

void StandardEquality::propagateFalseAssignment(Solver& solver, BooleanValue lit) {
    auto [source, target] = equalities.at(variableId(lit));
    if (isPositive(lit)) {
        // disequality
        VERIFY_NOT_REACHED();
    } else {
        // equality
        if (!connected(source, target))
            link(solver, source, target);
    }
}

StandardEquality::Link StandardEquality::linkFromReason(const Reason& reason) {
    return { std::bit_cast<Value>(reason.data1), std::bit_cast<Value>(reason.data2) };
}
Reason StandardEquality::linkToReason(Link l) const {
    return Reason {
        .reasonTheory = (uint32_t)ReasonTheory::theoryId(),
        .data1 = std::bit_cast<uint32_t>(l.source),
        .data2 = std::bit_cast<uint32_t>(l.target)
    };
}

bool StandardEquality::testReason(Solver&, const Reason& reason) {
    Link l = linkFromReason(reason);
    return connected(l.source, l.target);
}

void StandardEquality::onNewVariable(Solver&, int_t eqId) {
    checkInvariances();
    auto [source, target] = equalities.at(eqId);

    const auto& tInfo = equalityInfo(target);
    const int_t tIndex = tInfo.treeOffset;
    int_t watchInsertPos = tInfo.watchOffset;
    Watch watch { .otherValue = source, .eqId = (uint32_t)eqId };

    auto& rootInfo = equalityInfo(tInfo.root);
    rootInfo.watches.insert(rootInfo.watches.begin() + watchInsertPos, watch);

    const auto& tree = rootInfo.tree;
    int_t rootIndex = -1;
    for (;;) {
        if (rootIndex == tIndex)
            break;

        int_t index = rootIndex + 1;
        for (;;) {
            int_t nextIndex = index + tree[index].subTreeSize;
            if (nextIndex > tIndex)
                break;
            index = nextIndex;
        }
        rootIndex = index;

        auto& info = equalityInfo(tree[rootIndex].value);
        info.watches.insert(info.watches.begin() + (watchInsertPos - info.watchOffset), watch);
    }

    // Update indices after tIndex
    // TODO: Is this correct?
    for (int_t index = tIndex + 1; index < (int_t)tree.size(); index++)
        equalityInfo(tree[index].value).watchOffset += 1;

    checkInvariances();
}

ReasonTheory::ClauseAndIndex StandardEquality::reasonToClause(Solver& solver, const Reason& reason) {
    auto [a, b] = linkFromReason(reason);
    VERIFY(connected(a, b));

    auto& result = solver.scratchClause();
    result.push_back(equality(solver, a, b));

    struct StackEntry {
        int_t rootIndex;
        int_t aIndex;
        int_t bIndex;
    };
    std::vector<StackEntry> stack;

    Value treeRoot = equalityInfo(a).root;
    const TreeNode* tree = equalityInfo(treeRoot).tree.data();
    int_t rootIndex = -1;
    int_t aIndex = equalityInfo(a).treeOffset;
    int_t bIndex = equalityInfo(b).treeOffset;
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
        result.push_back(disequality(solver, sIndex == -1 ? treeRoot : tree[sIndex].value, tree[tIndex].value));
        stack.push_back({ index, tIndex, bIndex });
        bIndex = sIndex;
    }
    return { .clause = result, .forceLiteralIndex = 0 };
}

void StandardEquality::checkInvariances() {
    auto checkValue = [this](Value value) {
        const auto& info = equalityInfo(value);
        const auto& rootInfo = equalityInfo(info.root);
        if (info.treeOffset == -1) {
            VERIFY(info.root == value);
        } else {
            VERIFY(rootInfo.tree[info.treeOffset].value == value);
            VERIFY(rootInfo.tree[info.treeOffset].subTreeSize == info.tree.size() + 1);
        }
        VERIFY(std::equal(info.tree.begin(), info.tree.end(), rootInfo.tree.begin() + info.treeOffset + 1));
        VERIFY(std::equal(info.watches.begin(), info.watches.end(), rootInfo.watches.begin() + info.watchOffset));

        if (info.root == value) {
            for (int_t i = 1; i < (int_t)info.tree.size(); i++) {
                VERIFY(equalityInfo(info.tree[i - 1].value).watchOffset <= equalityInfo(info.tree[i].value).watchOffset);
            }
        }
    };
    for (int_t eqId = 0; eqId < variableCount(); eqId++) {
        auto [source, target] = equalities.at(eqId);
        checkValue(source);
        checkValue(target);
    }
}

}