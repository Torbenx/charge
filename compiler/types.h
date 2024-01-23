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
struct optional_traits;

template<typename T>
    requires requires { optional_traits<T>::empty_value; }
class std::optional<T> {
private:
    static constexpr auto empty_value = optional_traits<T>::empty_value;
    static_assert(!std::is_const_v<T>);
    static_assert(std::is_same_v<std::remove_const_t<decltype(empty_value)>, T>);
    static_assert(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_assignable_v<T>);

    T storage = empty_value;

public:
    constexpr optional() noexcept = default;
    constexpr optional(std::nullopt_t) noexcept
        : optional() { }
    constexpr optional(const optional& other) noexcept
        : storage(other.storage) { }
    constexpr optional(const T& value) noexcept
        : storage(value) { }

    constexpr optional& operator=(std::nullopt_t) noexcept {
        storage = empty_value;
        return *this;
    }
    constexpr optional& operator=(const optional& other) noexcept {
        storage = other.storage;
        return *this;
    }
    constexpr optional& operator=(const T& value) noexcept {
        storage = value;
        return *this;
    }

    constexpr bool has_value() const noexcept {
        return storage != empty_value;
    }
    constexpr explicit operator bool() const noexcept {
        return has_value();
    }
    constexpr void verify_has_value() const {
        VERIFY(has_value());
    }

    constexpr decltype(auto) operator->() const {
        verify_has_value();
        if constexpr (std::is_pointer_v<T>) {
            return (const T&)storage;
        } else {
            return (const T*)&storage;
        }
    }
    constexpr decltype(auto) operator->() {
        verify_has_value();
        if constexpr (std::is_pointer_v<T>) {
            return (T&)storage;
        } else {
            return (T*)&storage;
        }
    }
    constexpr auto& operator*() const {
        verify_has_value();
        if constexpr (std::is_pointer_v<T>) {
            return *storage;
        } else {
            return storage;
        }
    }
    constexpr auto& operator*() {
        verify_has_value();
        if constexpr (std::is_pointer_v<T>) {
            return *storage;
        } else {
            return storage;
        }
    }

    constexpr const T& value() const {
        verify_has_value();
        return storage;
    }
    constexpr T& value() {
        verify_has_value();
        return storage;
    }
    constexpr T value_or(T default_value) const noexcept {
        return has_value() ? storage : default_value;
    }

    constexpr void swap(optional& other) {
        std::swap(storage, other.storage);
    }
    constexpr void reset() {
        storage = empty_value;
    }

    constexpr operator T() const
        requires(std::is_pointer_v<T>)
    {
        verify_has_value();
        return storage;
    }

    constexpr ~optional() = default;
};

template<typename T>
struct optional_traits<T*> {
    static constexpr T* empty_value = nullptr;
};

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
template<typename Source, typename Target>
struct optional_traits<relative_pointer<Source, Target>> {
    static constexpr relative_pointer<Source, Target> empty_value = {};
};