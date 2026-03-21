#include <verify/ExpressionStorage.h>

namespace verify::expr_detail {

template<int_t idx, ExprKind kind, bool dependent>
auto get(const base<kind, dependent, idx>& b) {
    return std::bit_cast<data_t<kind, dependent, idx>>(b);
}

template<typename T>
concept HasPSorts = requires(const T& t) {
    { t.pSorts } -> std::same_as<const SortList&>;
};

template<ExprKind kind, int_t idx, size_t size>
auto extractPSort(const base<kind, true, idx>& b, std::array<SortList, size> in) {
    using T = data_t<kind, true, idx>;
    if constexpr (HasPSorts<T>) {
        std::array<SortList, size + 1> out;
        std::copy_n(in.begin(), size, out.begin());
        out[size] = std::bit_cast<T>(b).pSorts;
        return out;
    } else {
        return in;
    }
}

template<ExprKind kind>
auto extractPSorts(const compound<kind, true>& c) {
    return extractPSort<kind, 3>(c, extractPSort<kind, 2>(c, extractPSort<kind, 1>(c, std::array<SortList, 0> {})));
}

template<ExprKind kind, int_t idx>
basewrapper<kind, false, idx> getWrapperWithoutDependence(const compound<kind, true>& in) {
    using T = data_t<kind, false, idx>;
    if constexpr (std::is_void_v<T>)
        return {};
    else
        return static_cast<T>(get<idx>(in));
}

template<ExprKind kind>
compound<kind, false> removeDependence(const compound<kind, true>& in) {
    return compound<kind, false> {
        getWrapperWithoutDependence<kind, 1>(in),
        getWrapperWithoutDependence<kind, 2>(in),
        getWrapperWithoutDependence<kind, 3>(in)
    };
}

template<ExprKind kind, int_t idx>
basewrapper<kind, true, idx> getWrapperWithDependence(const compound<kind, false>& in, SortList pSorts) {
    using T = data_t<kind, false, idx>;
    using DT = data_t<kind, true, idx>;
    if constexpr (std::is_void_v<T>) {
        return {};
    } else if constexpr (HasPSorts<DT>) {
        return DT(get<idx>(in), pSorts);
    } else {
        return get<idx>(in);
    }
}

template<ExprKind kind>
compound<kind, true> withDependence(const compound<kind, false>& in, SortList pSorts) {
    return compound<kind, true> {
        getWrapperWithDependence<kind, 1>(in, pSorts),
        getWrapperWithDependence<kind, 2>(in, pSorts),
        getWrapperWithDependence<kind, 3>(in, pSorts)
    };
}

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

namespace verify {

using namespace expr_detail;

#define SIMPLE_EXPR(name, sortType)
#define EXPR(name, sortType, args...)                                                     \
    compound<ExprKind::name, false> ExpressionStorage::get##name(sortType key) const {    \
        VERIFY(key.kind() == ExprKind::name);                                             \
        return fromArray<compound<ExprKind::name, false>>(expressions[key.idBits]);       \
    }                                                                                     \
                                                                                          \
    sortType ExpressionStorage::add##name(const compound<ExprKind::name, false>& val) {   \
        uint32_t id = expressions.size();                                                 \
        expressions.push_back(toArray<compound<ExprKind::name, false>>(val));             \
        return (sortType)Expr(ExprKind::name, id);                                        \
    }                                                                                     \
                                                                                          \
    compound<ExprKind::name, true> ExpressionStorage::get##name(D##sortType key) const {  \
        return withDependence(get##name((sortType)key), key.pSorts);                      \
    }                                                                                     \
                                                                                          \
    D##sortType ExpressionStorage::add##name(const compound<ExprKind::name, true>& val) { \
        auto allSorts = extractPSorts(val);                                               \
        auto sorts = allSorts.front();                                                    \
        for (int_t i = 1; i < (int_t)allSorts.size(); i++)                                \
            VERIFY(std::ranges::equal(view(allSorts[i]), view(sorts)));                   \
        sortType r = add##name(removeDependence(val));                                    \
        return D<sortType> { r, sorts };                                                  \
    }

#include <verify/expressions.inc>

}