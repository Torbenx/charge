#pragma once

#include <verify/ir/Function.h>

#include <ranges>

namespace verify::ir {

struct Database {

    DeclHandle newDecl(DeclHandle parent) {
        uint32_t id = declarations.size();
        declarations.push_back({ .parent = parent, .children = {} });
        declarations[parent.id()].children.push_back(DeclHandle(id));
        return DeclHandle(id);
    }

private:
    struct MemberLit {
        Type type;
    };

    struct DeclarationNode {
        DeclHandle parent;
        std::vector<DeclHandle> children;
    };

    template<typename T>
    static ListBase makeListInternal(std::vector<T>& vec, std::span<const T> list) {
        uint32_t offset = vec.size();
        vec.insert(vec.end(), list.begin(), list.end());
        return { offset, (uint32_t)list.size() };
    }

    template<typename T>
    static std::span<const T> viewInternal(const std::vector<T>& vec, ListBase list) {
        VERIFY((int_t)list.m_offset + (int_t)list.m_size <= (int_t)vec.size());
        return { vec.data() + list.m_offset, list.m_size };
    }

    std::vector<MemberLit> memberLiterals;
    std::vector<DeclarationNode> declarations;
};

}