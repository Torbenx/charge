#pragma once

#include <EnumTable.h>

#include <utility>

namespace verify::backend {

struct SolverImpl;
struct SatCore;
struct Solver;

enum class ValueKind : uint8_t {
    Boolean,
    UninterpretedConstant,
    UninterpretedConstantSet,
    Member,
    // Type,
    // MemoryDeclaration,

    COUNT,
};

enum class TheoryId : uint8_t {
#define THEORY(name, valueKind) name,
#include <verify/backend/theories.inc>

    COUNT,
    Invalid = (uint8_t)limits::max
};

std::string_view nameString(TheoryId);

inline constexpr EnumTable<TheoryId, ValueKind> kindOf = {
#define THEORY(name, valueKind) { TheoryId::name, ValueKind::valueKind },
#include <verify/backend/theories.inc>
};

struct Value {
    constexpr Value(TheoryId theory, uint32_t id)
        : theoryBits(std::to_underlying(theory)), idBits(id) { }

    constexpr TheoryId theory() const { return (TheoryId)theoryBits; }
    constexpr uint32_t id() const { return idBits; }
    bool operator==(const Value&) const = default;

    uint32_t theoryBits : 8;
    uint32_t idBits : 24;
};
inline constexpr Value INVALID_VALUE = { TheoryId::Invalid, (uint32_t)limits::max };

//! A boolean value
/*!
In the literature on boolean satisfiability this would be called a literal.
For each literal X there exist the complementary literal NOT X.
*/
struct BooleanValue : Value {
    using Value::Value;
    constexpr explicit BooleanValue(Value v)
        : Value(v) { }

    constexpr BooleanValue operator!() const {
        return BooleanValue(theory(), id() ^ 1u);
    }

    constexpr bool negated() const {
        return (id() & 1u) != 0;
    }

    constexpr BooleanValue baseValue() const {
        return BooleanValue(theory(), id() & ~1u);
    }
};

inline constexpr BooleanValue true_literal = BooleanValue(TheoryId::TrueFalse, 0);
inline constexpr BooleanValue false_literal = BooleanValue(TheoryId::TrueFalse, 1);

struct Member : Value {
    using Value::Value;
    constexpr explicit Member(Value v)
        : Value(v) { }

    bool literal() const { return theory() == TheoryId::MemberLiterals; }
    bool composite() const { return theory() == TheoryId::CompositeMembers; }
    bool variable() const { return !literal() && !composite(); }
};

inline constexpr Member identity_member = Member(TheoryId::CompositeMembers, 0);

struct ClauseAndIndex {
    std::span<const BooleanValue> clause;
    int_t forceLiteralIndex = 0;
};

struct ClauseBuilder {
    uint32_t clauseId;

    explicit ClauseBuilder(uint32_t clauseId)
        : clauseId(clauseId) { }
    bool add(Solver&, BooleanValue);
};

//! Represents an unordered pair of values
/*!
These instances should always be normalized such that source < target in the rewrite order.
I.e. when equal all occurences of target should be rewritten to source.
*/
struct Pair {
    Value source;
    Value target;
};

//! Handle for an unordered pair of values
/*!
Has support for "special pairs" which consist of a value encoded in the handle
and a special (theory dependent) value. Currently this is used for bools
to encode (true_literal, _) and for sets to encode (empty_set, _).
*/
struct PairHandle {
    static constexpr PairHandle makeSpecialPair(Value value) {
        return PairHandle(value);
    }
    constexpr PairHandle(ValueKind kind, uint32_t id)
        : specialBit(0), kindBits(std::to_underlying(kind)), idBits(id) { }

    bool specialPair() const { return specialBit; }
    ValueKind valueKind() const {
        if (specialPair())
            return kindOf((TheoryId)kindBits);
        else
            return (ValueKind)kindBits;
    }
    uint32_t pairId() const {
        VERIFY(!specialPair());
        return idBits;
    }
    Value encodedValue() const {
        VERIFY(specialPair());
        return Value((TheoryId)kindBits, idBits);
    }

    static PairHandle decodeFromValue(Value v) {
        return { kindOf(v.theory()), v.id() };
    }
    Value encodeToValue(TheoryId theory) const {
        VERIFY(!specialPair());
        VERIFY(valueKind() == kindOf(theory));
        return Value(theory, pairId());
    }

    bool operator==(const PairHandle&) const = default;

private:
    constexpr explicit PairHandle(Value value)
        : specialBit(1), kindBits(std::to_underlying(value.theory())), idBits(value.id()) { }

    uint32_t specialBit : 1;
    uint32_t kindBits : 7;
    uint32_t idBits : 24;
};

constexpr ValueKind pairValueKind(TheoryId pairTheory) {
    static constexpr EnumTable<TheoryId, ValueKind> kinds = {
        ValueKind::COUNT,
        {
#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair) { TheoryId::name, ValueKind::pairValueKind },
#include <verify/backend/theories.inc>
        }
    };
    auto val = kinds(pairTheory);
    VERIFY(val < ValueKind::COUNT);
    return val;
}

constexpr uint8_t valuesPerPair(TheoryId pairTheory) {
    static constexpr EnumTable<TheoryId, uint8_t> vals = {
        limits::max,
        {
#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair) { TheoryId::name, valuesPerPair },
#include <verify/backend/theories.inc>
        }
    };
    auto val = vals(pairTheory);
    VERIFY(val != (uint8_t)limits::max);
    return val;
}

template<TheoryId theory>
PairHandle decodePairTheoryValue(Value v) {
#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair) \
    if constexpr (theory == TheoryId::name) {                            \
        VERIFY(v.theory() == theory);                                    \
        return { ValueKind::pairValueKind, v.id() / valuesPerPair };     \
    } else
#include <verify/backend/theories.inc>
    {
        // else case, should be static_assert(false) but must be dependent
        static_assert(theory == TheoryId::Invalid);
    }
}

template<TheoryId theory>
Value encodePairTheoryValue(PairHandle h) {
    VERIFY(!h.specialPair());
#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair) \
    if constexpr (theory == TheoryId::name) {                            \
        VERIFY(h.valueKind() == ValueKind::pairValueKind);               \
        return { theory, h.pairId() * valuesPerPair };                   \
    } else
#include <verify/backend/theories.inc>
    {
        // else case, should be static_assert(false) but must be dependent
        static_assert(theory == TheoryId::Invalid);
    }
}

}

template<std::derived_from<verify::backend::Value> T>
struct optional_traits<T> {
    static constexpr T empty_value = T(verify::backend::INVALID_VALUE);
};
template<>
struct optional_traits<verify::backend::PairHandle> {
    static constexpr verify::backend::PairHandle empty_value = verify::backend::PairHandle::makeSpecialPair(verify::backend::INVALID_VALUE);
};