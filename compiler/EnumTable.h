#pragma once

#include <types.h>

#include <utility>
#include <algorithm>

template<typename E, typename T>
struct EnumTable {
    static constexpr size_t N = std::to_underlying(E::COUNT);

    struct Entry {
        E key;
        T value;
        constexpr Entry(E key, T value)
            : key(key), value(value) { }
    };

    constexpr EnumTable(T defaultValue, std::initializer_list<Entry> entries)
        : EnumTable(defaultValue, std::make_index_sequence<N>()) {
        std::array<bool, N> checkArray;
        checkArray.fill(false);
        for (const Entry& entry : entries) {
            auto val = std::to_underlying(entry.key);
            VERIFY(!checkArray[val]);
            checkArray[val] = true;
            m_data[val] = entry.value;
        }
    }

    constexpr EnumTable(std::initializer_list<Entry> entries)
        : EnumTable(makeOptArray(entries), std::make_index_sequence<N>()) { }

    constexpr T operator()(E e) const {
        auto i = std::to_underlying(e);
        VERIFY(i >= 0 && i < N);
        return m_data[i];
    }

private:
    std::array<T, N> m_data;

    static constexpr auto makeOptArray(std::initializer_list<Entry> entries) {
        std::array<std::optional<T>, N> checkArray;
        for (const Entry& entry : entries) {
            auto val = std::to_underlying(entry.key);
            VERIFY(!checkArray[val].has_value());
            checkArray[val] = entry.value;
        }
        VERIFY(std::ranges::all_of(checkArray, [](std::optional<T> opt) { return opt.has_value(); }));
        return checkArray;
    }

    template<size_t... I>
    constexpr EnumTable(T defaultValue, std::index_sequence<I...>)
        : m_data { ((void)I, defaultValue)... } { }

    template<size_t... I>
    constexpr EnumTable(const std::array<std::optional<T>, N>& optArray, std::index_sequence<I...>)
        : m_data { (optArray[I].value())... } { }
};