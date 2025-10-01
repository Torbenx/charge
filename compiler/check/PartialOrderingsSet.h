#pragma once

#include <types.h>

#include <bitset>
#include <compare>

namespace check {

static_assert(std::bit_cast<std::partial_ordering>(int8_t { -1 }) == std::partial_ordering::less);
static_assert(std::bit_cast<std::partial_ordering>(int8_t { 0 }) == std::partial_ordering::equivalent);
static_assert(std::bit_cast<std::partial_ordering>(int8_t { 1 }) == std::partial_ordering::greater);
static_assert(std::bit_cast<std::partial_ordering>(int8_t { 2 }) == std::partial_ordering::unordered);

constexpr int_t poToIndex(std::partial_ordering ordering) { return std::bit_cast<int8_t>(ordering) + 1; }
constexpr std::partial_ordering poFromIndex(int_t index) {
    VERIFY(index >= 0 && index < 4);
    return std::bit_cast<std::partial_ordering>(int8_t(index));
}

struct PartialOrderingsSet {

    static consteval PartialOrderingsSet all() { return PartialOrderingsSet(true); }
    static consteval PartialOrderingsSet none() { return PartialOrderingsSet(false); }
    static consteval PartialOrderingsSet less() { return { std::partial_ordering::less }; }
    static consteval PartialOrderingsSet less_equal() { return { std::partial_ordering::less, std::partial_ordering::equivalent }; }
    static consteval PartialOrderingsSet equal() { return { std::partial_ordering::equivalent }; }
    static consteval PartialOrderingsSet greater_equal() { return { std::partial_ordering::equivalent, std::partial_ordering::greater }; }
    static consteval PartialOrderingsSet greater() { return { std::partial_ordering::greater }; }
    static consteval PartialOrderingsSet unordered() { return { std::partial_ordering::unordered }; }

    static constexpr PartialOrderingsSet intersect(PartialOrderingsSet a, PartialOrderingsSet b) {
        return PartialOrderingsSet(a.m_set & b.m_set);
    }

    constexpr PartialOrderingsSet(std::initializer_list<std::partial_ordering> orderings) {
        for (auto ordering : orderings)
            set(ordering, true);
    }

    constexpr void set(std::partial_ordering ordering, bool value = true) {
        m_set.set(poToIndex(ordering), value);
    }
    constexpr void clear() { m_set.reset(); }
    constexpr int_t count() const { return m_set.count(); }
    constexpr bool test(std::partial_ordering ordering) const {
        return m_set[poToIndex(ordering)];
    }

private:
    explicit constexpr PartialOrderingsSet(bool valueForAll) {
        m_set.set(0, valueForAll);
        m_set.set(1, valueForAll);
        m_set.set(2, valueForAll);
        m_set.set(3, valueForAll);
    }
    explicit constexpr PartialOrderingsSet(std::bitset<4> vals)
        : m_set(vals) { }

    std::bitset<4> m_set;
};

}