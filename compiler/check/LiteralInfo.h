#pragma once

#include <check/ValueTheory.h>

#include <bit>

namespace check {

//! Bitmask type for a clause. Contains 1 bit for each literal in the clause.
using clause_mask_t = uint64_t;
inline constexpr int_t MAX_CLAUSE_SIZE = sizeof(clause_mask_t) * 8;

struct LiteralInstance {
    static constexpr int_t LITERAL_BITS = std::bit_width((size_t)MAX_CLAUSE_SIZE - 1);
    static constexpr int_t MAX_CLAUSE_INDEX = ((int_t)1 << (32 - LITERAL_BITS)) - 1;

    uint32_t literalIndex : LITERAL_BITS = MAX_CLAUSE_SIZE - 1;
    uint32_t clauseIndex : 32 - LITERAL_BITS = MAX_CLAUSE_INDEX;

    bool operator==(const LiteralInstance&) const = default;
};

//!
struct TracePosition {
    uint32_t index;

    constexpr explicit TracePosition(uint32_t index)
        : index(index) { }

    auto operator<=>(const TracePosition&) const = default;
    bool operator==(const TracePosition&) const = default;
    TracePosition& operator++() {
        index += 1;
        return *this;
    }
    TracePosition operator++(int) {
        TracePosition copy = *this;
        index += 1;
        return copy;
    }
    friend TracePosition operator+(TracePosition l, int_t r) {
        return TracePosition(l.index + r);
    }
    friend TracePosition operator-(TracePosition l, int_t r) {
        return TracePosition(l.index - r);
    }
    TracePosition& operator+=(int_t r) {
        index += r;
        return *this;
    }
    TracePosition& operator-=(int_t r) {
        index -= r;
        return *this;
    }
};

}

template<>
struct optional_traits<check::TracePosition> {
    static constexpr check::TracePosition empty_value = check::TracePosition(-1);
};

namespace check {

struct BooleanTheory::LiteralInfo {
    std::optional<TracePosition> firstReason;
    std::optional<TracePosition> lastReason;

    std::optional<BooleanValue> nextPropagation;
    std::optional<BooleanValue> prevPropagation;

    uint32_t subTraceIndex = -1;
    uint32_t includedInNewClause = -1;

    std::vector<LiteralInstance> instances;

    bool assignedFalse() const { return firstReason.has_value(); }
};

}