#pragma once

#include <verify/ir/Expr.h>

#include <unordered_set>

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

struct SmtParameters { };
struct SatProof {
    struct Clause {
        Bool prop;
        Proof proof;
    };
    std::vector<Clause> clauses;
};
struct IntFarkasProof {
    std::vector<int32_t> coeff;
};

struct Function {

    FnHandle repr(Fn expr);

    CodePos here() const { return CodePos((uint32_t)m_instructions.size()); }
    Opcode opcodeAt(CodePos pos) const { return instRef(pos).opcode; }

    Sort sortOf(Expr) const;

    Expr addParameter(Sort sort) {
        Expr ret = Expr(ExprKind::FunctionParameter, parameterCount());
        m_parameters.push_back({ sort });
        return ret;
    }

    int_t parameterCount() const { return m_parameters.size(); }

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
    Theorem addTheorem(Bool prop, CodePos pos, Proof proof = Proof::makeSorry()) {
        Theorem result(m_theorems.size());
        m_theorems.push_back({ prop, pos, proof });
        return result;
    }
    void setProof(Theorem t, Proof p) {
        VERIFY(t.id() < m_theorems.size());
        m_theorems[t.id()].proof = p;
    }

    Theorem addPreCondition(Bool prop, CodePos pos) {
        Theorem result(m_theorems.size());
        m_theorems.push_back({ prop, pos, Proof(Tactic::Precondition, m_preConditions.size()) });
        m_preConditions.push_back(result);
        return result;
    }

    void addPostCondition(Theorem t) {
        m_postConditions.push_back(t);
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

#define INSTRUCTION(name, ...)                                              \
    void add##name(const function_detail::instruction_data<Opcode::name>&); \
    function_detail::instruction_data<Opcode::name> get##name(CodePos pos) const;
#include <verify/ir/instructions.inc>

#define COMPLEX_TACTIC(name, type)                  \
    Proof add##name(type proofData);                \
    const type& get##name(Proof proof) const {      \
        VERIFY(proof.id() < m_proofs##name.size()); \
        return m_proofs##name[proof.id()];          \
    }
#include <verify/ir/tactics.inc>

    void setJumpTarget(CodePos jumpInst, CodePos target);
    void setBranchTrueTarget(CodePos branchInst, CodePos target);
    void setBranchFalseTarget(CodePos branchInst, CodePos target);
    void setParent(CodePos phiInst, int_t index, CodePos parent);

    PhiParentList parents(CodePos phiInst) const { return getPhi(phiInst).parents; }

    CodePos parentPosition(PhiParent parent) const {
        VERIFY(parent.id() < m_phiParents.size());
        return m_phiParents[parent.id()];
    }

private:
    using expr_arr = std::array<uint32_t, 3>;
    using inst_arr = std::array<uint32_t, 3>;

    //! Identifies a compound expression that is already present in 'm_expressions'
    /*!
    The kind is part of the handle and the id indexes 'm_expressions', so the entry
    does not need to store any of the expression data itself. The hash is cached
    because rehashing must be possible without access to the owning function.
    */
    struct ExprEntry {
        Expr expr;
        size_t hash;
    };

    //! Transparent lookup key for a compound expression that has not been added yet
    struct ExprLookup {
        size_t hash;
        ExprKind kind;
        expr_arr data;
        const Function* function;
    };

    struct ExprHash {
        using is_transparent = void;

        size_t operator()(const ExprEntry& entry) const { return entry.hash; }
        size_t operator()(const ExprLookup& lookup) const { return lookup.hash; }
    };

    struct ExprHashEqual {
        using is_transparent = void;

        //! Entries are only ever inserted after a failed lookup, so they are known to be
        //! distinct and comparing the handles is sufficient here.
        bool operator()(const ExprEntry& a, const ExprEntry& b) const { return a.expr == b.expr; }
        bool operator()(const ExprLookup& a, const ExprEntry& b) const;
        bool operator()(const ExprEntry& a, const ExprLookup& b) const { return (*this)(b, a); }
    };

    Expr addExprInternal(ExprKind kind, expr_arr data);

    struct Inst {
        Opcode opcode;
        inst_arr data;
    };

    Inst& instRef(CodePos pos) {
        VERIFY(pos.id() < m_instructions.size());
        return m_instructions[pos.id()];
    }
    const Inst& instRef(CodePos pos) const {
        VERIFY(pos.id() < m_instructions.size());
        return m_instructions[pos.id()];
    }

    struct TheoremData {
        Bool prop;
        CodePos pos;
        Proof proof;
    };

    struct RelativePosData {
        std::vector<PhiParent> backedges;
        CodePos simplePos;
    };

    struct ParameterData {
        Sort sort;
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
    std::unordered_set<ExprEntry, ExprHash, ExprHashEqual> m_expressionSet;
    std::vector<TheoremData> m_theorems;
    std::vector<RelativePosData> m_relativePositions;
    std::vector<Expr> m_expressionLists;
    std::vector<CodePos> m_phiParents;
    std::vector<ParameterData> m_parameters;
    std::vector<Theorem> m_preConditions;
    std::vector<Theorem> m_postConditions;

#define COMPLEX_TACTIC(name, type) std::vector<type> m_proofs##name;
#include <verify/ir/tactics.inc>
};

}