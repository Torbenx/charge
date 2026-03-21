#pragma once

#include <verify/Expr.h>

namespace verify::expr_detail {

using arr = std::array<uint32_t, 3>;
struct D_unused;

template<typename>
struct extract_type;
template<typename T>
struct extract_type<void(T)> {
    using type = T;
};
template<>
struct extract_type<void(D_unused)> {
    using type = void;
};

template<ExprKind kind, bool dependent, int_t idx>
struct data;

#define SPECIALIZE_BINFO(name, dependent, idx, member) \
    template<>                                         \
    struct data<ExprKind::name, dependent, idx> {      \
        using type = extract_type<void(member)>::type; \
    };
#define SPECIALIZE_BINFOS(name, a, b, c, ...) \
    SPECIALIZE_BINFO(name, false, 1, a)       \
    SPECIALIZE_BINFO(name, false, 2, b)       \
    SPECIALIZE_BINFO(name, false, 3, c)
#define SPECIALIZE_DEPENDENT_BINFOS(name, a, b, c, ...) \
    SPECIALIZE_BINFO(name, true, 1, D##a)               \
    SPECIALIZE_BINFO(name, true, 2, D##b)               \
    SPECIALIZE_BINFO(name, true, 3, D##c)
#define COMPOUND_EXPR(name, sortType, args...)        \
    SPECIALIZE_BINFOS(name, args, D_unused, D_unused) \
    SPECIALIZE_DEPENDENT_BINFOS(name, args, _unused, _unused)
#include <verify/expressions.inc>

template<ExprKind kind, bool dependent, int_t idx>
using data_t = typename data<kind, dependent, idx>::type;

template<ExprKind kind, bool dependent, int_t>
struct base;

#define DECLARE_BASE(name, dependent, idx, member) \
    template<>                                     \
    struct base<ExprKind::name, dependent, idx> {  \
        member;                                    \
    };

#define EXPR_BASES_HELPER(name, a, b, c, ...) \
    DECLARE_BASE(name, false, 1, a)           \
    DECLARE_BASE(name, false, 2, b)           \
    DECLARE_BASE(name, false, 3, c)
#define EXPR_BASES(name, ...) EXPR_BASES_HELPER(name, __VA_ARGS__, , )

#define EXPR_DEPENDENT_BASES_HELPER(name, a, b, c, ...) \
    DECLARE_BASE(name, true, 1, D##a)                   \
    DECLARE_BASE(name, true, 2, D##b)                   \
    DECLARE_BASE(name, true, 3, D##c)
#define EXPR_DEPENDENT_BASES(name, ...) EXPR_DEPENDENT_BASES_HELPER(name, __VA_ARGS__, _unused static const _unused, _unused static const _unused)

#define COMPOUND_EXPR(name, sortType, args...) \
    EXPR_BASES(name, args)                     \
    EXPR_DEPENDENT_BASES(name, args)
#include <verify/expressions.inc>

template<ExprKind kind, bool dependent, int_t idx>
struct basewrapper : base<kind, dependent, idx> {
    basewrapper(data_t<kind, dependent, idx> in)
        : base<kind, dependent, idx>(std::bit_cast<base<kind, dependent, idx>>(in)) { }
};
template<ExprKind kind, bool dependent, int_t idx>
    requires std::is_void_v<data_t<kind, dependent, idx>>
struct basewrapper<kind, dependent, idx> : base<kind, dependent, idx> {
    basewrapper()
        : base<kind, dependent, idx>() { }
};

template<ExprKind kind, bool dependent>
struct compound : basewrapper<kind, dependent, 1>, basewrapper<kind, dependent, 2>, basewrapper<kind, dependent, 3> { };

}

namespace verify {

struct ExpressionStorage {
private:
    using arr = expr_detail::arr;

public:
#define COMPOUND_EXPR(name, sortType, args...)                                   \
    expr_detail::compound<ExprKind::name, false> get##name(sortType r) const;    \
    sortType add##name(const expr_detail::compound<ExprKind::name, false>& val); \
    expr_detail::compound<ExprKind::name, true> get##name(D##sortType r) const;  \
    D##sortType add##name(const expr_detail::compound<ExprKind::name, true>& val);
#include <verify/expressions.inc>

private:
    std::vector<arr> expressions;
};

}