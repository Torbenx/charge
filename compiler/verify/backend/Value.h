#pragma once

#include <types.h>

#include <utility>

namespace verify::backend {

struct SolverImpl;
struct SatCore;
struct Solver;

enum class ValueKind : uint8_t {
    Boolean,
    Type,
    Member,
    MemoryDeclaration,

    COUNT,
};

enum class TheoryId : uint8_t {
#define THEORY(name, valueKind) name,
#include <verify/backend/theories.inc>

    COUNT,
};

inline ValueKind kindOf(TheoryId theory) {
#define THEORY(name, valueKind) \
    case TheoryId::name:        \
        return ValueKind::valueKind;
    switch (theory) {
#include <verify/backend/theories.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

struct Value {
    Value(TheoryId theory, uint32_t id)
        : theoryBits(std::to_underlying(theory)), idBits(id) { }

    TheoryId theory() const { return (TheoryId)theoryBits; }
    uint32_t id() const { return idBits; }
    bool operator==(const Value&) const = default;

    uint32_t theoryBits : 8;
    uint32_t idBits : 24;
};

//! A boolean value
/*!
In the literature on boolean satisfiability this would be called a literal.
For each literal X there exist the complementary literal NOT X.
*/
struct BooleanValue : Value {
    using Value::Value;
    explicit BooleanValue(Value v)
        : Value(v) { }

    BooleanValue negated() const {
        return BooleanValue(theory(), id() ^ 1u);
    }

    BooleanValue operator!() const { return negated(); }
};

struct ClauseAndIndex {
    std::span<const BooleanValue> clause;
    int_t forceLiteralIndex = 0;
};

struct ClauseBuilder {
    uint32_t clauseId;

    explicit ClauseBuilder(uint32_t clauseId)
        : clauseId(clauseId) { }
    void add(Solver&, BooleanValue);
};

}