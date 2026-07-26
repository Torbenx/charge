#include <verify/ir/Function.h>

#include <algorithm>

namespace verify::ir {

using arr = std::array<uint32_t, 3>;

template<typename T>
static arr toArray(const T& in) {
    // The unused words are zeroed below and the expressions are compared as a whole, so the
    // representation must not contain any padding bits.
    static_assert(std::has_unique_object_representations_v<T>);
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

bool Function::ExprHashEqual::operator()(const ExprLookup& a, const ExprEntry& b) const {
    return a.kind == b.expr.kind() && a.data == a.function->m_expressions[b.expr.idBits].data;
}

static size_t hashExpr(ExprKind kind, const arr& data) {
    size_t hash = std::to_underlying(kind);
    for (uint32_t word : data)
        hash_combine(hash, word);
    return hash;
}

/*!
Expressions are uniqued by their kind and their kind erased data. Since all handles appearing
in the data are themselves unique, comparing the data words is enough to identify an expression.

TODO: Relative code positions are only compared bitwise, so 'Load' expressions on complex
positions with equal contents are not yet recognized as equal. The positions themselves will
have to be uniqued to fix that.
*/
Expr Function::addExprInternal(ExprKind kind, expr_arr data) {
    size_t hash = hashExpr(kind, data);
    auto it = m_expressionSet.find(ExprLookup { hash, kind, data, this });
    if (it != m_expressionSet.end())
        return it->expr;

    uint32_t id = m_expressions.size();
    VERIFY(id == Expr(kind, id).id());
    m_expressions.push_back({ kind, data });
    Expr result(kind, id);
    m_expressionSet.emplace(ExprEntry { result, hash });
    return result;
}

std::optional<Expr> Function::findExprInternal(ExprKind kind, expr_arr data) const {
    auto it = m_expressionSet.find(ExprLookup { hashExpr(kind, data), kind, data, this });
    if (it == m_expressionSet.end())
        return std::nullopt;
    return it->expr;
}

template<ExprKind kind, typename SortType>
Sort Function::sortOfKind(Expr expr) const {
    if constexpr (kind == ExprKind::FunctionParameter) {
        VERIFY(expr.id() < m_parameters.size());
        return m_parameters[expr.id()].sort;
    } else if constexpr (kind == ExprKind::PureScalarReturn) {
        // TODO: This requires the signature of the called function
        VERIFY_NOT_REACHED();
    } else {
        return SortType::sort;
    }
}

Sort Function::sortOf(Expr expr) const {
    switch (expr.kind()) {
#define EXPR(name, sortType) \
    case ExprKind::name:     \
        return sortOfKind<ExprKind::name, sortType>(expr);
#include <verify/ir/expressions.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

Function::LoadData Function::getLoad(Expr expr) const {
    VERIFY(isLoad(expr.kind()));
    return fromArray<LoadData>(m_expressions[expr.idBits].data);
}

Expr Function::addLoad(Sort sort, const LoadData& data) {
    return addExprInternal(loadKind(sort), toArray<LoadData>(data));
}

//! The data of every 'Loadable' expression, independent of its sort
using loadable_data = function_detail::compound_expr<ExprKind::LoadableBool>;

MemoryLoc Function::getLoadableLocation(Expr expr) const {
    VERIFY(isLoadable(expr.kind()));
    return fromArray<loadable_data>(m_expressions[expr.idBits].data).loc;
}

Bool Function::addLoadable(Sort sort, MemoryLoc loc) {
    return Bool(addExprInternal(loadableKind(sort), toArray<loadable_data>({ loc })));
}

std::optional<Bool> Function::findLoadable(Sort sort, MemoryLoc loc) const {
    std::optional<Expr> expr = findExprInternal(loadableKind(sort), toArray<loadable_data>({ loc }));
    if (!expr.has_value())
        return std::nullopt;
    return Bool(*expr);
}

std::optional<Sort> Function::loadableSort(MemoryLoc loc) const {
    // TODO: A location with several 'Loadable' theorems is contradictory and has to be
    // rejected when the function is checked. Until then the first sort found is used.
    for (int_t i = 0; i < std::to_underlying(Sort::COUNT); i++) {
        Sort sort = (Sort)i;
        std::optional<Bool> loadable = findLoadable(sort, loc);
        if (loadable.has_value() && findTheorem(*loadable).has_value())
            return sort;
    }
    return std::nullopt;
}

bool Function::ExprListHashEqual::operator()(const ExprListLookup& a, const ExprListEntry& b) const {
    return std::ranges::equal(a.list, viewInternal(a.function->m_expressionLists, b.list));
}

static size_t hashExprList(std::span<const Expr> list) {
    size_t hash = list.size();
    for (Expr expr : list)
        hash_combine(hash, std::bit_cast<uint32_t>(expr));
    return hash;
}

//! Expression lists are uniqued by their contents so that expressions containing them can be uniqued too
ExprList Function::makeExprList(std::span<const Expr> list) {
    size_t hash = hashExprList(list);
    auto it = m_expressionListSet.find(ExprListLookup { hash, list, this });
    if (it != m_expressionListSet.end())
        return it->list;

    ExprList result = (ExprList)makeListInternal(m_expressionLists, list);
    m_expressionListSet.emplace(ExprListEntry { result, hash });
    return result;
}

std::optional<Theorem> Function::findTheorem(Bool prop) const {
    auto it = m_theoremByProp.find(prop);
    if (it == m_theoremByProp.end())
        return std::nullopt;
    return it->second;
}

//! Theorems are uniqued by their proposition, so a proposition must not be proven twice
Theorem Function::addTheorem(Bool prop, CodePos pos, Proof proof) {
    auto [it, inserted] = m_theoremByProp.try_emplace(prop, Theorem(m_theorems.size()));
    VERIFY(inserted);
    m_theorems.push_back({ prop, pos, proof });
    return it->second;
}

Theorem Function::addPreCondition(Bool prop, CodePos pos) {
    Theorem result = addTheorem(prop, pos, Proof(Tactic::Precondition, m_preConditions.size()));
    m_preConditions.push_back(result);
    return result;
}

#define COMPOUND_EXPR(name, sortType, args...)                                                            \
    function_detail::compound_expr<ExprKind::name> Function::get##name(sortType key) const {              \
        VERIFY(key.kind() == ExprKind::name);                                                             \
        return fromArray<function_detail::compound_expr<ExprKind::name>>(m_expressions[key.idBits].data); \
    }                                                                                                     \
                                                                                                          \
    sortType Function::add##name(const function_detail::compound_expr<ExprKind::name>& val) {             \
        return (sortType)addExprInternal(ExprKind::name,                                                  \
            toArray<function_detail::compound_expr<ExprKind::name>>(val));                                \
    }
#include <verify/ir/expressions.inc>

#define INSTRUCTION(name, args...)                                                                                  \
    void Function::add##name(const function_detail::instruction_data<Opcode::name>& data) {                         \
        m_instructions.push_back({ Opcode::name, toArray<function_detail::instruction_data<Opcode::name>>(data) }); \
    }                                                                                                               \
    function_detail::instruction_data<Opcode::name> Function::get##name(CodePos pos) const {                        \
        const Inst& inst = instRef(pos);                                                                            \
        VERIFY(inst.opcode == Opcode::name);                                                                        \
        return fromArray<function_detail::instruction_data<Opcode::name>>(inst.data);                               \
    }
#include <verify/ir/instructions.inc>

#define COMPLEX_TACTIC(name, type)                         \
    Proof Function::add##name(type proofData) {            \
        Proof result(Tactic::name, m_proofs##name.size()); \
        m_proofs##name.emplace_back(std::move(proofData)); \
        return result;                                     \
    }
#include <verify/ir/tactics.inc>

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

void Function::setParent(CodePos phiInst, int_t index, CodePos parent) {
    using data_t = function_detail::instruction_data<Opcode::Phi>;
    Inst& inst = instRef(phiInst);
    VERIFY(inst.opcode == Opcode::Phi);
    auto data = fromArray<data_t>(inst.data);
    VERIFY(index >= 0 && index < data.parents.size());
    m_phiParents[data.parents.m_offset + (uint32_t)index] = parent;
}

}