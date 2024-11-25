#pragma once

#include <ranges>
#include <types.h>

struct TreeLabel {
    //! Returns the label for the root node
    static TreeLabel rootLabel() { return TreeLabel(std::numeric_limits<uint32_t>::max() >> 1); }

    //! Returns the label for left or right child of this node
    [[nodiscard]] TreeLabel extend(bool right) const {
        uint32_t tmp1 = m_label + 1;
        uint32_t exLabel = tmp1 & (right ? (uint32_t)-1 : m_label);
        exLabel |= (tmp1 ^ m_label) >> 2;
        return TreeLabel(exLabel);
    }

    //! Returns the depth of this node
    /*!
    The root node is defined to have depth 0.
    */
    int_t depth() const { return 31 - std::countr_one(m_label); }

    uint32_t label() const { return m_label; }

private:
    TreeLabel(uint32_t label)
        : m_label(label) { }

    uint32_t m_label;
};

namespace FlatTreeSetDetail {

inline constexpr uint32_t NIL_HANDLE = -1;

struct NodeBase {
    uint32_t leftChild = NIL_HANDLE;
    uint32_t rightChild = NIL_HANDLE;
    TreeLabel label;

    explicit NodeBase(TreeLabel label)
        : label(label) { }

    NodeBase(const NodeBase&) = delete;
    NodeBase& operator=(const NodeBase&) = delete;
    NodeBase(NodeBase&&) = default;
    NodeBase& operator=(NodeBase&&) = default;
};

template<typename data_type, typename array_type>
struct Node : NodeBase {
    data_type data;
    uint32_t arraySize;
    Node(TreeLabel label, data_type data, uint32_t arraySize)
        : NodeBase(label), data(std::move(data)), arraySize(arraySize) { }
};
template<typename data_type>
struct Node<data_type, void> : NodeBase {
    data_type data;
    explicit Node(TreeLabel label, data_type data)
        : NodeBase(label), data(std::move(data)) { }
};

template<typename Impl, typename data_type, typename array_type = void>
struct Base {
protected:
    using node_type = Node<data_type, array_type>;
    Impl* impl() { return static_cast<Impl*>(this); }

    template<typename... Args>
    uint32_t get(Args&&... args) {
        if (rootNode == NIL_HANDLE) {
            rootNode = impl()->makeNode(args..., TreeLabel::rootLabel());
            return rootNode;
        }
        uint32_t handle = rootNode;
        for (;;) {
            node_type& node = node_at(handle);
            auto c = impl()->compare(args..., node.data);
            if (c == 0)
                return handle;
            uint32_t& nextHandle = c < 0 ? node.leftChild : node.rightChild;
            if (nextHandle != NIL_HANDLE) {
                handle = nextHandle;
                continue;
            }

            uint32_t result = nodes.size();
            nextHandle = result;
            VERIFY(result == impl()->makeNode(args..., node.label.extend(c > 0)));
            return result;
        }
    }

    node_type& node_at(uint32_t handle) {
        return *reinterpret_cast<node_type*>(&nodes[handle]);
    }
    const node_type& node_at(uint32_t handle) const {
        return *reinterpret_cast<const node_type*>(&nodes[handle]);
    }
    uint32_t nextNodeHandle() { return nodes.size(); }
    uint32_t makeNode(TreeLabel label, data_type data)
        requires std::is_void_v<array_type>
    {
        uint32_t handle = nextNodeHandle();
        nodes.emplace_back(label, std::move(data));
        return handle;
    }
    uint32_t makeNode(TreeLabel label, const data_type& data, std::span<const array_type> array)
        requires(!std::is_void_v<array_type>)
    {
        static_assert(alignof(array_type) <= 4);
        uint32_t handle = nextNodeHandle();
        nodes.resize(nodes.size() + (sizeof(node_type) + array.size() * sizeof(array_type)) / 4);
        std::construct_at(&node_at(handle), label, data, array.size());
        std::copy(array.begin(), array.end(), reinterpret_cast<array_type*>(&node_at(handle) + 1));
        return handle;
    }

    std::vector<std::conditional_t<std::is_void_v<array_type>, node_type, uint32_t>> nodes;
    uint32_t rootNode = NIL_HANDLE;

public:
    data_type& at(uint32_t handle)
        requires std::is_void_v<array_type>
    {
        return node_at(handle).data;
    }
    const data_type& at(uint32_t handle) const
        requires std::is_void_v<array_type>
    {
        return node_at(handle).data;
    }
    std::pair<data_type&, std::span<array_type>> at(uint32_t handle)
        requires(!std::is_void_v<array_type>)
    {
        return {
            node_at(handle).data,
            std::span(reinterpret_cast<array_type*>(&node_at(handle) + 1), node_at(handle).arraySize)
        };
    }
    std::pair<const data_type&, std::span<const array_type>> at(uint32_t handle) const
        requires(!std::is_void_v<array_type>)
    {
        return {
            node_at(handle).data,
            std::span(reinterpret_cast<const array_type*>(&node_at(handle) + 1), node_at(handle).arraySize)
        };
    }

    uint32_t label(uint32_t handle) const { return node_at(handle).label.label(); }

    auto entries() const
        requires std::is_void_v<array_type>
    {
        auto view = std::views::transform(nodes, [](const node_type& node) -> const data_type& { return node.data; });
        static_assert(std::random_access_iterator<decltype(view.begin())>);
        return view;
    }

    int_t size() const
        requires std::is_void_v<array_type>
    { return nodes.size(); }
};

}