#include <verify/ir/Function.h>

namespace verify::ir {

using arr = std::array<uint32_t, 3>;

template<typename T>
static arr toArray(const T& in) {
    if constexpr (sizeof(T) == 3 * sizeof(uint32_t)) {
        return std::bit_cast<arr>(in);
    } else if constexpr (sizeof(T) == 2 * sizeof(uint32_t)) {
        auto tmp = std::bit_cast<std::array<uint32_t, 2>>(in);
        return arr { tmp[0], tmp[1], 0u };
    } else {
        auto tmp = std::bit_cast<std::array<uint32_t, 1>>(in);
        return arr { tmp[0], 0u, 0u };
    }
}

template<typename T>
static T fromArray(const arr& in) {
    if constexpr (sizeof(T) == 3 * sizeof(uint32_t)) {
        return std::bit_cast<T>(in);
    } else if constexpr (sizeof(T) == 2 * sizeof(uint32_t)) {
        std::array<uint32_t, 2> tmp { in[0], in[1] };
        return std::bit_cast<T>(tmp);
    } else {
        std::array<uint32_t, 1> tmp { in[0] };
        return std::bit_cast<T>(tmp);
    }
}

#define COMPOUND_EXPR(name, sortType, args...)                                                       \
    function_detail::compound_expr<ExprKind::name> Function::get##name(sortType key) const {         \
        VERIFY(key.kind() == ExprKind::name);                                                        \
        return fromArray<function_detail::compound_expr<ExprKind::name>>(m_expressions[key.idBits]); \
    }                                                                                                \
                                                                                                     \
    sortType Function::add##name(const function_detail::compound_expr<ExprKind::name>& val) {        \
        uint32_t id = m_expressions.size();                                                          \
        m_expressions.push_back(toArray<function_detail::compound_expr<ExprKind::name>>(val));       \
        return (sortType)Expr(ExprKind::name, id);                                                   \
    }

#include <verify/ir/expressions.inc>

void Function::setJumpTarget(CodePos jumpInst, CodePos target) {
    using data_t = function_detail::instruction_data<Opcode::Jump>;
    Inst& inst = instRef(jumpInst);
    VERIFY(inst.opcode == Opcode::Jump);
    auto data = fromArray<data_t>(inst.data);
    data.target = target;
    inst.data = toArray<data_t>(data);
}

void Function::setBranchTrueTarget(CodePos branchInst, CodePos target) {
    using data_t = function_detail::instruction_data<Opcode::Branch>;
    Inst& inst = instRef(branchInst);
    VERIFY(inst.opcode == Opcode::Branch);
    auto data = fromArray<data_t>(inst.data);
    data.ifTrue = target;
    inst.data = toArray<data_t>(data);
}

void Function::setBranchFalseTarget(CodePos branchInst, CodePos target) {
    using data_t = function_detail::instruction_data<Opcode::Branch>;
    Inst& inst = instRef(branchInst);
    VERIFY(inst.opcode == Opcode::Branch);
    auto data = fromArray<data_t>(inst.data);
    data.ifFalse = target;
    inst.data = toArray<data_t>(data);
}

void Function::setPhiParent(CodePos phiInst, int_t index, CodePos parent) {
    using data_t = function_detail::instruction_data<Opcode::Phi>;
    Inst& inst = instRef(phiInst);
    VERIFY(inst.opcode == Opcode::Phi);
    auto data = fromArray<data_t>(inst.data);
    VERIFY(index >= 0 && index < data.parents.size());
    m_phiParents[data.parents.m_offset + (uint32_t)index] = parent;
}

}