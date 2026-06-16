#pragma once

#include <verify/ir/Expr.h>

namespace verify::ir::expr_detail {

using arr = std::array<uint32_t, 3>;

struct unused_tag;
template<typename>
struct extract_type;
template<typename T>
struct extract_type<void(T)> {
    using type = T;
};
template<>
struct extract_type<void(unused_tag)> {
    using type = void;
};

template<ExprKind kind, int_t idx>
struct data;

#define SPECIALIZE_BINFO(name, idx, member)            \
    template<>                                         \
    struct data<ExprKind::name, idx> {                 \
        using type = extract_type<void(member)>::type; \
    };
#define SPECIALIZE_BINFOS(name, a, b, c, ...) \
    SPECIALIZE_BINFO(name, 1, a)              \
    SPECIALIZE_BINFO(name, 2, b)              \
    SPECIALIZE_BINFO(name, 3, c)
#define COMPOUND_EXPR(name, sortType, args...) \
    SPECIALIZE_BINFOS(name, args, unused_tag, unused_tag)
#include <verify/ir/expressions.inc>

template<ExprKind kind, int_t idx>
using data_t = typename data<kind, idx>::type;

template<ExprKind kind>
struct compound;

#define COMPOUND_MEMBER(a, b, c, ...) \
    a;                                \
    b;                                \
    c;
#define COMPOUND_EXPR(name, sort, args...) \
    template<>                             \
    struct compound<ExprKind::name> {      \
        COMPOUND_MEMBER(args, , )          \
    };
#include <verify/ir/expressions.inc>

}

namespace verify::ir {

    struct ExpressionStorage {
    private:
        using arr = expr_detail::arr;

    public:
#define COMPOUND_EXPR(name, sortType, args...)                         \
    expr_detail::compound<ExprKind::name> get##name(sortType r) const; \
    sortType add##name(const expr_detail::compound<ExprKind::name>& val);
#include <verify/ir/expressions.inc>

    private:
        std::vector<arr> expressions;
    };

}