#pragma once

#include <verify/ir/Expr.h>

#include <unordered_map>
#include <unordered_set>

namespace verify::ir::function_detail {

//! An \p bytes sized struct guaranteed to contain zeros
template<int_t bytes>
struct Padding {
    constexpr Padding() { storage.fill((std::byte)0); }

private:
    std::array<std::byte, bytes> storage;
};

//! Stands in for any member of 'T' during aggregate initialization
/*!
The conversion to 'T' itself has to be excluded, otherwise initializing an aggregate with a
single member would be ambiguous with copying it.
*/
template<typename T>
struct AnyMember {
    template<typename U>
        requires(!std::same_as<T, std::remove_cvref_t<U>>)
    operator U() const;
};

//! The number of members of an aggregate
/*!
It is the largest number of initializers the aggregate accepts. The larger counts have to be
tried first because an aggregate does not accept fewer initializers than it has members when the
remaining members cannot be value initialized.
*/
template<typename T>
constexpr int_t memberCount() {
    if constexpr (requires { T { AnyMember<T> {}, AnyMember<T> {}, AnyMember<T> {} }; })
        return 3;
    else if constexpr (requires { T { AnyMember<T> {}, AnyMember<T> {} }; })
        return 2;
    else if constexpr (requires { T { AnyMember<T> {} }; })
        return 1;
    else
        static_assert(false, "Only aggregates with one to three members are supported");
}

//! Calls 'callback' for every member of an aggregate
template<typename T, typename Callback>
void forEachMember(const T& value, Callback&& callback) {
    if constexpr (memberCount<T>() == 3) {
        const auto& [a, b, c] = value;
        callback(a);
        callback(b);
        callback(c);
    } else if constexpr (memberCount<T>() == 2) {
        const auto& [a, b] = value;
        callback(a);
        callback(b);
    } else {
        const auto& [a] = value;
        callback(a);
    }
}

//! Calls 'callback' for the members of two aggregates of the same type pairwise
template<typename T, typename Callback>
void forEachMemberPair(const T& left, const T& right, Callback&& callback) {
    if constexpr (memberCount<T>() == 3) {
        const auto& [a, b, c] = left;
        const auto& [x, y, z] = right;
        callback(a, x);
        callback(b, y);
        callback(c, z);
    } else if constexpr (memberCount<T>() == 2) {
        const auto& [a, b] = left;
        const auto& [x, y] = right;
        callback(a, x);
        callback(b, y);
    } else {
        const auto& [a] = left;
        const auto& [x] = right;
        callback(a, x);
    }
}

template<typename T>
constexpr bool is_padding = false;
template<int_t bytes>
constexpr bool is_padding<Padding<bytes>> = true;

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

//! The theorems whose propositions are taken as the clauses of the SAT problem
/*!
A clause is a theorem of the function like any other, so it is proven and checked on its own and
the proof only refers to it. The same theorem may be a clause of more than one proof.
*/
struct SatProof {
    std::vector<Theorem> clauses;
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
    std::optional<Theorem> findTheorem(Bool prop) const;

    //! All theorems of the function in the order they were added
    auto theorems() const {
        return std::views::iota(0u, (uint32_t)m_theorems.size())
            | std::views::transform([](uint32_t id) { return Theorem(id); });
    }

    //! Adds a theorem for a proposition that is not proven yet
    Theorem addTheorem(Bool prop, CodePos pos, Proof proof);

    //! The proposition of a theorem never changes, so only the proof may be replaced
    void setProof(Theorem t, Proof p) {
        VERIFY(t.id() < m_theorems.size());
        m_theorems[t.id()].proof = p;
    }

    Theorem addPreCondition(Bool prop, CodePos pos);

    void addPostCondition(Theorem t) {
        m_postConditions.push_back(t);
    }

    ExprList makeExprList(std::span<const Expr> list);

    std::span<const Expr> view(ExprList list) const { return viewInternal(m_expressionLists, list); }

    void addPhi(std::span<const CodePos> sources);
    int_t controlFlowEdgeCount() const { return m_controlFlowEdges.size(); }

#define VARIADIC_EXPR(name, sortType, operandSortType)                        \
    sortType add##name(std::span<const operandSortType>);                     \
    sortType add##name(std::initializer_list<operandSortType> operands) {     \
        return add##name((std::span<const operandSortType>)operands);         \
    }                                                                         \
    function_detail::compound_expr<ExprKind::name> get##name(sortType) const; \
    std::optional<sortType> find##name(const function_detail::compound_expr<ExprKind::name>&) const;
#define COMPOUND_EXPR(name, sortType, args...)                                                       \
    function_detail::compound_expr<ExprKind::name> get##name(sortType) const;                        \
    std::optional<sortType> find##name(const function_detail::compound_expr<ExprKind::name>&) const; \
    sortType add##name(const function_detail::compound_expr<ExprKind::name>&);
#include <verify/ir/expressions.inc>

    int_t expressionCount() const { return m_expressions.size(); }

    //! All compound expressions of the function in the order they were added
    auto expressions() const {
        return std::views::iota(0u, (uint32_t)m_expressions.size())
            | std::views::transform([this](uint32_t id) { return Expr(m_expressions[id].kind, id); });
    }

    //! Loads of all sorts share the same data, so they can be accessed without knowing the sort
    struct LoadData {
        MemoryLoc loc;
        RelativeCodePos pos;
    };
    LoadData getLoad(Expr) const;
    Expr addLoad(Sort, const LoadData&);

    std::optional<Sort> scalarSort(Type) const;
    //! Returns the sort a location may be loaded with according to its type
    std::optional<Sort> scalarSort(MemoryLoc) const;

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
    void setEdgeSource(CodePos phiInst, int_t index, CodePos source);

    ControlFlowEdgeList incomingEdges(CodePos phiInst) const { return getPhi(phiInst).incomingEdges; }

    CodePos edgeSource(ControlFlowEdge edge) const {
        VERIFY(edge.id() < m_controlFlowEdges.size());
        return m_controlFlowEdges[edge.id()].source;
    }

    CodePos edgeTarget(ControlFlowEdge edge) const {
        VERIFY(edge.id() < m_controlFlowEdges.size());
        return m_controlFlowEdges[edge.id()].target;
    }

private:
    using expr_arr = std::array<uint32_t, 3>;
    using inst_arr = std::array<uint32_t, 3>;

#define VARIADIC_EXPR(name, sortType, operandSortType) \
    sortType add##name(const function_detail::compound_expr<ExprKind::name>&);
#include <verify/ir/expressions.inc>

    template<ExprKind kind, std::derived_from<Expr> OperandSort>
    ExprList flattenOperands(std::span<const OperandSort> operands);

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
    std::optional<Expr> findExprInternal(ExprKind kind, expr_arr data) const;

    //! Determines the sort of a single expression kind
    /*!
    All kinds but 'FunctionParameter' and 'PureScalarReturn' have the sort they are
    declared with in 'expressions.inc'.
    */
    template<ExprKind kind, typename SortType>
    Sort sortOfKind(Expr) const;

    //! Identifies an expression list that is already present in 'm_expressionLists'
    /*!
    The handle contains the offset and the size, so as for 'ExprEntry' the contents do
    not have to be repeated here and the hash is cached for rehashing.
    */
    struct ExprListEntry {
        ExprList list;
        size_t hash;
    };

    //! Transparent lookup key for an expression list that has not been added yet
    struct ExprListLookup {
        size_t hash;
        std::span<const Expr> list;
        const Function* function;
    };

    struct ExprListHash {
        using is_transparent = void;

        size_t operator()(const ExprListEntry& entry) const { return entry.hash; }
        size_t operator()(const ExprListLookup& lookup) const { return lookup.hash; }
    };

    struct ExprListHashEqual {
        using is_transparent = void;

        //! As for 'ExprHashEqual' entries are only inserted after a failed lookup
        bool operator()(const ExprListEntry& a, const ExprListEntry& b) const {
            return a.list.m_offset == b.list.m_offset && a.list.m_size == b.list.m_size;
        }
        bool operator()(const ExprListLookup& a, const ExprListEntry& b) const;
        bool operator()(const ExprListEntry& a, const ExprListLookup& b) const { return (*this)(b, a); }
    };

    //! Hashes the proposition of a theorem
    /*!
    Propositions are uniqued expressions, so the handle can be hashed and compared directly
    and no further information has to be kept next to it.
    */
    struct PropHash {
        size_t operator()(Bool prop) const {
            size_t hash = 0;
            hash_combine(hash, std::bit_cast<uint32_t>((Expr)prop));
            return hash;
        }
    };

    //! The kind is kept next to the data so that the expressions can be enumerated
    struct ExprData {
        ExprKind kind;
        expr_arr data;
    };

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
        std::vector<ControlFlowEdge> backedges;
        CodePos simplePos;
    };

    struct ParameterData {
        Sort sort;
    };

    struct ControlFlowEdgeInfo {
        CodePos source;
        CodePos target;
    };

    template<typename T>
    static ListBase makeListInternal(std::vector<T>& vec, std::span<const T> list) {
        uint32_t offset = vec.size();
        vec.append_range(list);
        return { offset, (uint32_t)list.size() };
    }

    template<typename T>
    static std::span<const T> viewInternal(const std::vector<T>& vec, ListBase list) {
        VERIFY((int_t)list.m_offset + (int_t)list.m_size <= (int_t)vec.size());
        return { vec.data() + list.m_offset, list.m_size };
    }

    std::vector<Inst> m_instructions;
    std::vector<ExprData> m_expressions;
    std::unordered_set<ExprEntry, ExprHash, ExprHashEqual> m_expressionSet;
    std::vector<TheoremData> m_theorems;
    //! Only stays valid because the proposition of a theorem is never changed
    std::unordered_map<Bool, Theorem, PropHash> m_theoremByProp;
    std::vector<RelativePosData> m_relativePositions;
    std::vector<Expr> m_expressionLists;
    std::unordered_set<ExprListEntry, ExprListHash, ExprListHashEqual> m_expressionListSet;
    std::vector<ControlFlowEdgeInfo> m_controlFlowEdges;
    std::vector<ParameterData> m_parameters;
    std::vector<Theorem> m_preConditions;
    std::vector<Theorem> m_postConditions;

#define COMPLEX_TACTIC(name, type) std::vector<type> m_proofs##name;
#include <verify/ir/tactics.inc>
};

}