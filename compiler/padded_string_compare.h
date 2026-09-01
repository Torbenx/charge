#pragma once

#include <types.h>

#include <cstring>

#if CHARGE_SSE_OPTIMIZATIONS
#include <immintrin.h>
#endif

//! Number of readable bytes a padded string must carry behind its content
inline constexpr int_t PADDED_STRING_PADDING = 16;

namespace padded_string_compare_detail {
constexpr bool constexpr_compare_eq(const char* a, const char* b, int_t length) {
    for (int_t i = 0; i < length; i++) {
        if (a[i] != b[i])
            return false;
    }
    return true;
}
}

//! padded_string_compare_eq() for a \p length that is known to be less than \ref PADDED_STRING_PADDING
[[nodiscard]] constexpr bool padded_small_string_compare_eq(const char* a, const char* b, int_t length) {
    VERIFY(length >= 0 && length <= PADDED_STRING_PADDING);
    if !consteval {
#if CHARGE_SSE_OPTIMIZATIONS
        __m128i left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
        __m128i right = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
        unsigned equal = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(left, right)));
        // Clears every bit from `length` upwards, a single bzhi with BMI2.
        return (~equal & ((1u << length) - 1)) == 0;
#else
        return std::memcmp(a, b, static_cast<size_t>(length)) == 0;
#endif
    } else {
        return padded_string_compare_detail::constexpr_compare_eq(a, b, length);
    }
}

//! Compares \p length characters of \p a and \p b for equality
/*!
Both pointers have to be readable up to the next multiple of PADDED_STRING_PADDING.
*/
[[nodiscard]] constexpr bool padded_string_compare_eq(const char* a, const char* b, int_t length) {
    VERIFY(length >= 0);
    if !consteval {
#if CHARGE_SSE_OPTIMIZATIONS
        while (length > PADDED_STRING_PADDING) {
            __m128i left = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
            __m128i right = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
            if (_mm_movemask_epi8(_mm_cmpeq_epi8(left, right)) != 0xffff)
                return false;
            a += PADDED_STRING_PADDING;
            b += PADDED_STRING_PADDING;
            length -= PADDED_STRING_PADDING;
        }
        return padded_small_string_compare_eq(a, b, length);
#else
        return std::memcmp(a, b, static_cast<size_t>(length)) == 0;
#endif
    } else {
        return padded_string_compare_detail::constexpr_compare_eq(a, b, length);
    }
}
