#pragma once

#include <verify/ir/Function.h>

#include <ranges>

namespace verify::ir {

struct Database {

    struct MemberUseInInvariant {
        uint32_t memberIndex;
        uint32_t invariantIndex;
    };

    DeclHandle newDecl(DeclHandle parent);

    FnHandle newFn();
    const Function& fn(FnHandle fn) {
        VERIFY(fn.id() < m_functions.size());
        return m_functions[fn.id()];
    }
    void setFn(FnHandle fn, Function f) {
        VERIFY(fn.id() < m_functions.size());
        m_functions[fn.id()] = std::move(f);
    }

    TypeImpl containingType(MemberLiteral member) const {
        return memberInfo(member).containingType;
    }
    FnHandle typeFn(MemberLiteral member) const {
        return memberInfo(member).typeFn.value();
    }
    std::span<const Invariant> usingInvariants(MemberLiteral member) const {
        return memberInfo(member).usingInvariants;
    }
    void setTypeFn(MemberLiteral member, FnHandle fn) {
        memberInfo(member).typeFn = fn;
    }

    TypeImpl targetType(Invariant inv) const {
        return invariantInfo(inv).targetType;
    }
    FnHandle invariantFn(Invariant inv) const {
        return invariantInfo(inv).fn.value();
    }
    std::span<const MemberLiteral> usedMembers(Invariant inv) const {
        return invariantInfo(inv).usedMembers;
    }
    void setInvariantFn(Invariant inv, FnHandle fn) {
        invariantInfo(inv).fn = fn;
    }


    TypeImpl addTypeImpl(
        std::span<const std::optional<FnHandle>> memberTypeFns,
        std::span<const std::optional<FnHandle>> invariantFns,
        std::span<const MemberUseInInvariant> usageMap);
    MemberLiteralList members(TypeImpl impl) const {
        return typeImplInfo(impl).members;
    }
    InvariantList invariants(TypeImpl impl) const {
        return typeImplInfo(impl).invariants;
    }

private:
    struct MemberInfo {
        TypeImpl containingType;
        std::optional<FnHandle> typeFn;
        std::vector<Invariant> usingInvariants;
    };

    struct InvariantInfo {
        TypeImpl targetType;
        std::optional<FnHandle> fn;
        std::vector<MemberLiteral> usedMembers;
    };

    struct TypeImplInfo {
        MemberLiteralList members;
        InvariantList invariants;
    };

    struct DeclarationNode {
        DeclHandle parent;
        std::vector<DeclHandle> children;
    };

    MemberInfo& memberInfo(MemberLiteral member) {
        VERIFY(member.id() < m_members.size());
        return m_members[member.id()];
    }
    const MemberInfo& memberInfo(MemberLiteral member) const {
        VERIFY(member.id() < m_members.size());
        return m_members[member.id()];
    }
    InvariantInfo& invariantInfo(Invariant inv) {
        VERIFY(inv.id() < m_invariants.size());
        return m_invariants[inv.id()];
    }
    const InvariantInfo& invariantInfo(Invariant inv) const {
        VERIFY(inv.id() < m_invariants.size());
        return m_invariants[inv.id()];
    }
    TypeImplInfo& typeImplInfo(TypeImpl impl) {
        VERIFY(impl.id() < m_typeImpls.size());
        return m_typeImpls[impl.id()];
    }
    const TypeImplInfo& typeImplInfo(TypeImpl impl) const {
        VERIFY(impl.id() < m_typeImpls.size());
        return m_typeImpls[impl.id()];
    }

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

    std::vector<MemberInfo> m_members;
    std::vector<InvariantInfo> m_invariants;
    std::vector<TypeImplInfo> m_typeImpls;
    std::vector<Function> m_functions;
    std::vector<DeclarationNode> m_declarations;
};

}