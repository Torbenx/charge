#include <check/StandardEquality.h>

namespace check {

std::vector<StandardEquality::Link> StandardEquality::path(Value a, Value b) {
    VERIFY(connected(a, b));
    struct StackEntry {
        int_t rootIndex;
        int_t aIndex;
        int_t bIndex;
    };
    std::vector<StackEntry> stack;

    std::vector<Link> result;
    Value treeRoot = equalityInfo(a).root;
    const TreeNode* tree = equalityInfo(treeRoot).tree.data();
    int_t rootIndex = -1;
    int_t aIndex = equalityInfo(a).index;
    int_t bIndex = equalityInfo(b).index;
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
        result.push_back({ sIndex == -1 ? treeRoot : tree[sIndex].value, tree[tIndex].value });
        stack.push_back({ index, tIndex, bIndex });
        bIndex = sIndex;
    }
    return result;
}

void StandardEquality::link(Value source, Value target) {
    VERIFY(!connected(source, target));
    const auto& sourceInfo = equalityInfo(source);
    const auto& targetInfo = equalityInfo(target);
    auto& sourceTree = equalityInfo(sourceInfo.root).tree;
    auto& targetRootInfo = equalityInfo(targetInfo.root); // Careful: targetRootInfo and targetInfo may alias
    const auto& targetTree = targetRootInfo.tree;

    int_t oldSourceTreeSize = sourceTree.size();
    sourceTree.push_back({ targetInfo.root, (uint32_t)(targetTree.size() + 1), (uint32_t)(sourceInfo.index + 1), (uint32_t)(targetInfo.index + 1) });

    sourceTree.insert(sourceTree.end(), targetTree.begin(), targetTree.end());

    targetRootInfo.root = sourceInfo.root;
    targetRootInfo.index = oldSourceTreeSize;
    for (int_t i = 0; i < (int_t)targetTree.size(); i++) {
        auto& info = equalityInfo(targetTree[i].value);
        info.root = sourceInfo.root;
        info.index = oldSourceTreeSize + 1 + i;
    }

    trace.push_back({ source, target });
}

}