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