#include <verify/ir/Database.h>

namespace verify::ir {

DeclHandle Database::newDecl(DeclHandle parent) {
    uint32_t id = m_declarations.size();
    m_declarations.push_back({ .parent = parent, .children = {} });
    m_declarations[parent.id()].children.push_back(DeclHandle(id));
    return DeclHandle(id);
}

TypeImpl Database::addTypeImpl(
    std::span<const std::optional<FnHandle>> memberTypeFns,
    std::span<const std::optional<FnHandle>> invariantFns,
    std::span<const MemberUseInInvariant> usageMap) {
    TypeImpl impl { (uint32_t)m_typeImpls.size() };
    MemberLiteralList members { (uint32_t)m_members.size(), (uint32_t)memberTypeFns.size() };
    InvariantList invariants { (uint32_t)m_invariants.size(), (uint32_t)invariantFns.size() };
    m_typeImpls.push_back({ members, invariants });

    for (auto fn : memberTypeFns)
        m_members.push_back({ impl, fn, {} });
    for (auto fn : invariantFns)
        m_invariants.push_back({ impl, fn, {} });
    for (auto entry : usageMap) {
        MemberLiteral member = members.at(entry.memberIndex);
        Invariant inv = invariants.at(entry.invariantIndex);
        m_members[member.id()].usingInvariants.push_back(inv);
        m_invariants[inv.id()].usedMembers.push_back(member);
    }

    return impl;
}

FnHandle Database::newFn() {
    FnHandle fn { (uint32_t)m_functions.size() };
    m_functions.emplace_back();
    return fn;
}

}