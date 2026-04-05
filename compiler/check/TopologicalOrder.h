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

        std::vector<std::optional<NodeId>> maskVec;
        maskVec.resize(sNode.orderIndex - tNode.orderIndex + 1);
        auto mask = [&maskVec, offset = tNode.orderIndex](int_t index) -> auto& { return maskVec[index - offset]; };
        std::vector<NodeId> relocatedNodes;

        int_t index = tNode.orderIndex;
        int_t writeIndex = index;
        mask(tNode.orderIndex) = source;
        for (; index <= (int_t)sNode.orderIndex; index++) {
            NodeId parentId = order[index];
            if (mask(index).has_value()) {
                for (auto childId : at(parentId).children) {
                    const Node& childNode = at(childId);
                    if (childNode.orderIndex <= sNode.orderIndex && !mask(childNode.orderIndex).has_value()) {
                        // childNode.orderIndex >= tNode.orderIndex is guaranteed
                        mask(childNode.orderIndex) = parentId;
                    }
                }
                relocatedNodes.push_back(parentId);
            } else {
                order[writeIndex] = parentId;
                at(parentId).orderIndex = writeIndex;
                writeIndex += 1;
            }
        }
        if (maskVec.back().has_value()) {
            // cycle
        }
        for (int_t relocIndex = 0; relocIndex < (int_t)relocatedNodes.size(); writeIndex++, relocIndex++) {
            order[writeIndex] = relocatedNodes[relocIndex];
            at(relocatedNodes[relocIndex]).orderIndex = writeIndex;
        }
        sNode.children.push_back(target);
    }

    bool operator()(NodeId left, NodeId right) const { return at(left).orderIndex < at(right).orderIndex; }
};