#include <verify/ir/ExpressionStorage.h>

namespace verify::ir::expr_detail {

template<typename T>
arr toArray(const T& in) {
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
T fromArray(const arr& in) {
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

}

namespace verify::ir {

using namespace expr_detail;

#define COMPOUND_EXPR(name, sortType, args...)                                   \
    compound<ExprKind::name> ExpressionStorage::get##name(sortType key) const {  \
        VERIFY(key.kind() == ExprKind::name);                                    \
        return fromArray<compound<ExprKind::name>>(expressions[key.idBits]);     \
    }                                                                            \
                                                                                 \
    sortType ExpressionStorage::add##name(const compound<ExprKind::name>& val) { \
        uint32_t id = expressions.size();                                        \
        expressions.push_back(toArray<compound<ExprKind::name>>(val));           \
        return (sortType)Expr(ExprKind::name, id);                               \
    }

#include <verify/ir/expressions.inc>

}