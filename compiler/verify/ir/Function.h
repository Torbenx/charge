#pragma once

#include <verify/ir/Expr.h>

namespace verify::ir::function_detail {

template<ExprKind kind>
struct compound_expr;

#define COMPOUND_MEMBER(a, b, c, ...) \
    a;                                \
    b;                                \
    c;
#define COMPOUND_EXPR(name, sort, args...) \
    template<>                             \
    struct compound_expr<ExprKind::name> { \
        COMPOUND_MEMBER(args, , )          \
    };
#include <verify/ir/expressions.inc>
#undef COMPOUND_MEMBER

template<Opcode op>
struct instruction_data;

#define INSTRUCTION_DATA(a, b, c, ...) \
    a;                                 \
    b;                                 \
    c;
#define INSTRUCTION(name, args...)          \
    template<>                              \
    struct instruction_data<Opcode::name> { \
        INSTRUCTION_DATA(args, , )          \
    };
#include <verify/ir/instructions.inc>
#undef INSTRUCTION_DATA

}

namespace verify::ir {

struct Function {

    FnHandle repr(Fn expr);

    CodePos here() const { return CodePos((uint32_t)m_instructions.size()); }

    Sort sortOf(Expr) const;

    const auto& inst(CodePos pos) const {
        VERIFY(pos.id() < m_instructions.size());
        return m_instructions[pos.id()];
    }

    Bool prop(Theorem t) const {
        VERIFY(t.id() < m_theorems.size());
        return m_theorems[t.id()].prop;
    }
    Proof proof(Theorem t) const {
        VERIFY(t.id() < m_theorems.size());
        return m_theorems[t.id()].proof;
    }
    CodePos position(Theorem t) const {
        VERIFY(t.id() < m_theorems.size());
        return m_theorems[t.id()].pos;
    }

    ExprList makeExprList(std::span<const Expr> list) {
        return (ExprList)makeListInternal(m_expressionLists, list);
    }

    PhiParentList makePhiParentList(std::span<const CodePos> list) {
        return (PhiParentList)makeListInternal(m_phiParents, list);
    }

#define COMPOUND_EXPR(name, sortType, args...)                                \
    function_detail::compound_expr<ExprKind::name> get##name(sortType) const; \
    sortType add##name(const function_detail::compound_expr<ExprKind::name>&);
#include <verify/ir/expressions.inc>

#define INSTRUCTION(name, ...) \
    void add##name(const function_detail::instruction_data<Opcode::name>&);
#include <verify/ir/instructions.inc>

    void setJumpTarget(CodePos jumpInst, CodePos target);
    void setBranchTrueTarget(CodePos branchInst, CodePos target);
    void setBranchFalseTarget(CodePos branchInst, CodePos target);
    void setParent(CodePos phiInst, int_t index, CodePos parent);

    PhiParentList parents(CodePos phiInst) const;

    CodePos parentPosition(PhiParent parent) const {
        VERIFY(parent.id() < m_phiParents.size());
        return m_phiParents[parent.id()];
    }

private:
    using expr_arr = std::array<uint32_t, 3>;
    using inst_arr = std::array<uint32_t, 3>;

    struct Inst {
        Opcode opcode;
        inst_arr data;
    };

    Inst& instRef(CodePos pos) {
        VERIFY(pos.id() < m_instructions.size());
        return m_instructions[pos.id()];
    }

    struct TheoremData {
        Bool prop;
        Proof proof;
        CodePos pos;
    };

    struct RelativePosData {
        std::vector<PhiParent> backedges;
        CodePos simplePos;
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

    std::vector<Inst> m_instructions;
    std::vector<expr_arr> m_expressions;
    std::vector<TheoremData> m_theorems;
    std::vector<RelativePosData> m_relativePositions;
    std::vector<Expr> m_expressionLists;
    std::vector<CodePos> m_phiParents;
};

}