#pragma once

#include <ranges>
#include <vector>

struct TopologicalOrder {
    struct NodeId {
        uint32_t id;
    };
    struct Node {
        //! Index in the topological order
        uint32_t orderIndex = 0;

        //! Children of this node in no particular order
        std::vector<NodeId> children = {};
    };

    std::vector<Node> nodes;
    std::vector<NodeId> order;

    Node& at(NodeId id) { return nodes[id.id]; }
    const Node& at(NodeId id) const { return nodes[id.id]; }

    NodeId newNode() {
        NodeId id { (uint32_t)nodes.size() };
        nodes.push_back({ (uint32_t)order.size() });
        order.push_back(id);
        return id;
    }

    void addEdge(NodeId source, NodeId target) {
        Node& sNode = at(source);
        Node& tNode = at(target);
        if (sNode.orderIndex < tNode.orderIndex) {
            sNode.children.push_back(target);
            return;
        }

        std::vector<bool> mask;
        mask.resize(sNode.orderIndex - tNode.orderIndex + 1);
        std::vector<NodeId> relocatedNodes;

        int_t index = 0;
        int_t writeIndex = index;
        NodeId* subOrder = order.data() + tNode.orderIndex;
        for (; index < (int_t)mask.size(); index++) {
            if (mask[index]) {
                for (auto childId : at(subOrder[index]).children) {
                    const Node& childNode = at(childId);
                    if (childNode.orderIndex <= sNode.orderIndex) // childNode.orderIndex >= tNode.orderIndex is guaranteed
                        mask[childNode.orderIndex - tNode.orderIndex] = true;
                }
                relocatedNodes.push_back(subOrder[index]);
            } else {
                subOrder[writeIndex] = subOrder[index];
                at(subOrder[index]).orderIndex = tNode.orderIndex + writeIndex;
                writeIndex += 1;
            }
        }
        if (mask.back()) {
            // cycle
        }
        for (int_t relocIndex = 0; relocIndex <(int_t)relocatedNodes.size(); writeIndex++, relocIndex++) {
            subOrder[writeIndex] = relocatedNodes[relocIndex];
            at(relocatedNodes[relocIndex]).orderIndex = tNode.orderIndex + writeIndex;
        }
        std::copy(relocatedNodes.begin(), relocatedNodes.end(), order.begin() + writeIndex);
        sNode.children.push_back(target);
    }

    bool operator()(NodeId left, NodeId right) const { return at(left).orderIndex < at(right).orderIndex; }
};