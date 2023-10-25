#pragma once

#include "log.h"
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>

using int_t = std::ptrdiff_t;
template<int_t... Is>
using int_sequence = std::integer_sequence<int_t, Is...>;
template<int_t N>
using make_int_sequence = std::make_integer_sequence<int_t, N>;
template<typename... Ts>
using int_sequence_for = make_int_sequence<sizeof...(Ts)>;

constexpr size_t alignmentCeil(size_t in, size_t alignment) {
    return (in + alignment - 1) & ~(alignment - 1);
}

template<typename T>
struct id {
    T* ptr;
    constexpr id(T* ptr)
        : ptr(ptr) { VERIFY(ptr != nullptr); }
    constexpr T& operator*() const { return *ptr; }
    constexpr T* operator->() const { return ptr; }
    constexpr operator T*() const { return ptr; }
    operator uintptr_t() const { return ptr; }
};

template<typename Source, typename Target>
struct relative_pointer {
    using relative_offset_type = int32_t;

    relative_offset_type aligned_relative_offset = 0;
    constexpr relative_pointer() = default;
    constexpr relative_pointer(Source* source, Target* target) {
        if (target != nullptr) {
            int_t offset = (reinterpret_cast<std::byte*>(target) - reinterpret_cast<std::byte*>(source));
            VERIFY(offset >= std::numeric_limits<relative_offset_type>::min()
                && offset <= std::numeric_limits<relative_offset_type>::max());
            aligned_relative_offset = (relative_offset_type)offset;
        }
    }

    constexpr int_t relative_offset_bytes() const {
        return (int_t)aligned_relative_offset;
    }

    constexpr Target* get(Source* source) const {
        if (aligned_relative_offset == 0)
            return nullptr;
        return reinterpret_cast<Target*>(reinterpret_cast<std::byte*>(source) + relative_offset_bytes());
    }
};