#pragma once

#include <types.h>

#include <bit>
#include <limits>
#include <type_traits>

template<typename T>
union MaybeUninitialized {
    MaybeUninitialized() { }
    T value;
};

//! Discriminated union between an integer and a payload type
template<typename I, typename T>
    requires(std::is_integral_v<I>)
struct IntOr {
    static_assert(std::endian::native == std::endian::little && !std::is_same_v<I, bool>);
    static_assert(sizeof(I) >= sizeof(T));
    using U = std::make_unsigned_t<I>;

    //! Construct a variant holding a payload
    /*!
    Pointer payloads need to be aligned to two bytes.
    */
    static IntOr forPayload(T payload) {
        if constexpr (std::is_pointer_v<T>) {
            static_assert(sizeof(T) == sizeof(I));
            U payloadAsU = std::bit_cast<U>(payload);
            if constexpr (alignof(std::remove_pointer_t<T>) == 1)
                VERIFY((payloadAsU & (U)1) == (U)0);
            return IntOr(payloadAsU | (U)1);
        } else {
            static_assert(std::is_trivially_copyable_v<T>);
            U payloadAsU = 0;
            __builtin_memcpy(&payloadAsU, &payload, sizeof(payload));
            if constexpr (sizeof(T) == sizeof(I))
                VERIFY(payloadAsU < (U)1 << (std::numeric_limits<U>::digits - 1));
            return IntOr((payloadAsU << 1) | (U)1);
        }
    }

    //! Construct a variant holding an integer
    /*!
    For an N-bit I the value is silently truncated to N-1 bits.
    */
    static IntOr forInt(I value) {
        return IntOr((U)value << 1);
    }

    //! Returns if this object holds an integer
    bool isInt() const { return (m_u & (U)1) == (U)0; }

    //! Returns the integer in this variant
    I get() const {
        VERIFY(isInt());
        // Cast to result type first to perform an arithmetic shift for signed types.
        return (I)m_u >> 1;
    }

    //! Returns the integer in this variant multiplied by two
    I getScaled() const {
        VERIFY(isInt());
        return static_cast<I>(m_u);
    }

    U rawBits() const { return m_u; }

    //! Returns the payload in this variant
    T payload() const {
        VERIFY(!isInt());
        if constexpr (std::is_pointer_v<T>) {
            return std::bit_cast<T>(m_u ^ (U)1);
        } else {
            U payloadAsU = m_u >> 1;
            MaybeUninitialized<T> result;
            __builtin_memcpy(&result.value, &payloadAsU, sizeof(T));
            return result.value;
        }
    }

private:
    explicit IntOr(U u)
        : m_u(u) { }

    //! Internal storage
    /*!
    Store either i*2 or (payload << 1)|1. For pointer payloads the layout is instead pointer|1.
    Lowest bit discriminates between the alternatives.
    */
    U m_u;
};

template<typename T>
using Int8Or = IntOr<int8_t, T>;
template<typename T>
using UInt8Or = IntOr<uint8_t, T>;
template<typename T>
using Int16Or = IntOr<int16_t, T>;
template<typename T>
using UInt16Or = IntOr<uint16_t, T>;
template<typename T>
using Int32Or = IntOr<int32_t, T>;
template<typename T>
using UInt32Or = IntOr<uint32_t, T>;
template<typename T>
using Int64Or = IntOr<int64_t, T>;
template<typename T>
using UInt64Or = IntOr<uint64_t, T>;