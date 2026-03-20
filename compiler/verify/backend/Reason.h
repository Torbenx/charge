#pragma once

#include <types.h>

#include <utility>

namespace verify::backend {

enum class ReasonKind : uint8_t {
#define REASON(name, ...) name,
#include <verify/backend/reasons.inc>
};

inline bool isFullyPropagating(ReasonKind kind) {
#define REASON(name, data, propagating, ...) \
    case ReasonKind::name:                   \
        return propagating;
    switch (kind) {
#include <verify/backend/reasons.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

template<ReasonKind>
struct reason_data;

#define REASON(name, data, ...)            \
    struct data;                           \
    template<>                             \
    struct reason_data<ReasonKind::name> { \
        using type = data;                 \
    };
#include <verify/backend/reasons.inc>

template<ReasonKind kind>
using reason_data_t = typename reason_data<kind>::type;

template<typename T>
std::array<std::byte, 8> packReasonData(T data) {
    auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(data);
    std::array<std::byte, 8> outBytes;
    std::copy_n(bytes.begin(), bytes.size(), outBytes.begin());
    std::fill_n(outBytes.begin() + bytes.size(), outBytes.size() - bytes.size(), (std::byte)0);
    return outBytes;
}

template<typename T>
struct bitwidth_of {
    static constexpr size_t v = sizeof(T) * 8;
};
template<>
struct bitwidth_of<bool> {
    static constexpr size_t v = 1;
};
template<typename T>
    requires(std::is_enum_v<T> && std::is_same_v<decltype(T::COUNT), T>)
struct bitwidth_of<T> {
    static constexpr size_t v = std::bit_width(T::COUNT - 1);
};

template<typename... Ts>
constexpr size_t bitlength = (bitwidth_of<Ts>::v + ...);

template<size_t offset, typename T, typename... Tags>
struct bitoffset_helper;
template<size_t offset, typename T, typename... Tags>
struct bitoffset_helper<offset, T, T, Tags...> {
    static constexpr size_t v = offset - bitwidth_of<T>::v;
};
template<size_t offset, typename T, typename Tag0, typename... Tags>
struct bitoffset_helper<offset, T, Tag0, Tags...> {
    static constexpr size_t v = bitoffset_helper<offset - bitwidth_of<Tag0>::v, T, Tags...>::v;
};
template<typename T, typename... Tags>
constexpr size_t bitoffset = bitoffset_helper<bitlength<Tags...>, T, Tags...>::v;

template<typename... Tags>
uint32_t packReasonTags(Tags... tags) {
    uint32_t result = 0;
    ((result |= uint32_t(tags), result <<= bitwidth_of<Tags>::v), ...);
    return result;
}

template<typename Data, typename... Tags>
struct PackedReason {
    PackedReason(Data data, Tags... tags)
        : kindBits(0), tagBits(packReasonTags(tags...)), dataBytes(packReasonData(data)) { }

    template<typename T>
    T tag() const {
        static constexpr size_t offset = bitoffset<T, Tags...>;
        static constexpr size_t width = bitwidth_of<T>::v;
        static constexpr uint32_t mask = ((uint32_t)1 << width) - (uint32_t)1;
        return T((tagBits >> offset) & mask);
    }

    uint32_t kindBits : 8;
    uint32_t tagBits : 24;
    std::array<std::byte, 8> dataBytes;
};

struct Reason {
    Reason(ReasonKind kind, uint32_t tag, const std::array<std::byte, 8>& bytes)
        : kindBits(std::to_underlying(kind))
        , tagBits(tag)
        , dataBytes(bytes) { }

    ReasonKind kind() const { return ReasonKind(kindBits); }
    bool isDecision() const { return kind() == ReasonKind::Decision; }

    template<ReasonKind k>
    reason_data_t<k> get() const {
        VERIFY(kind() == k);
        using T = reason_data_t<k>;
        if constexpr (sizeof(T) <= 8) {
            std::array<std::byte, sizeof(T)> out;
            std::copy_n(dataBytes.begin(), out.size(), out.begin());
            return std::bit_cast<T>(out);
        } else {
            return std::bit_cast<T>(*this);
        }
    }

    uint32_t kindBits : 8;
    uint32_t tagBits : 24;
    std::array<std::byte, 8> dataBytes;
};
static_assert(sizeof(Reason) == 12);

template<ReasonKind kind>
Reason makeReason(const reason_data_t<kind>& data) {
    using T = reason_data_t<kind>;
    if constexpr (sizeof(T) <= 8) {
        return Reason(kind, 0, packReasonData(data));
    } else {
        Reason out = std::bit_cast<Reason>(data);
        out.kindBits = std::to_underlying(kind);
        return out;
    }
}

struct EmptyReasonData { };

}