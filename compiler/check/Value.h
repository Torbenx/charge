#pragma once

#include <types.h>

namespace check {

struct Solver;

inline constexpr int_t SOLVER_INTERNAL_VARS_THEORY_ID = 0;

inline constexpr int_t ENTRY_BLOCKS_THEORY_ID = 0;

enum class ValueKind : uint8_t {
    Boolean,
    Type,
    MemoryLocation,
    MemberExpression,
};

//! A value
/*!
A value is identified by its theory and its id within that theory.
*/
struct Value {
    uint32_t theoryId : 8 = -1;
    uint32_t valueId : 24 = -1;

    bool operator==(const Value& other) const = default;
};

struct OrientedPair {
    static OrientedPair orient(Solver& solver, Value a, Value b);

    Value source;
    Value target;

    bool operator==(const OrientedPair&) const = default;
};

//! A boolean value
/*!
In the literature on boolean satisfiability this would be called a literal.
For each literal X there exist the complementary literal NOT X. These will always belong to the same theory.
\see BooleanTheory::negate()
*/
struct BooleanValue : Value { };

//! A memory location value
struct MemoryLocation : Value { };

//! A member expression value
/*!
In this context a member can be either a member of a composite type or an element of an array.
*/
struct MemberExpression : Value { };

struct Type : Value { };

struct BlockId {
    uint32_t theoryId : 8 = -1;
    uint32_t blockId : 24 = -1;

    bool operator==(const BlockId&) const = default;
};

//! Represent an execution position immediately after the given instruction
struct CodePosition {
    BlockId block = {};
    uint32_t position = 0;

    bool operator==(const CodePosition&) const = default;
};

struct Load {
    MemoryLocation location;
    CodePosition position;
};

namespace builtins {
    inline constexpr BooleanValue true_literal = { SOLVER_INTERNAL_VARS_THEORY_ID, 0 };
    inline constexpr BooleanValue false_literal = { SOLVER_INTERNAL_VARS_THEORY_ID, 1 };

    inline constexpr BlockId entry_block = { ENTRY_BLOCKS_THEORY_ID, 0 };

    inline constexpr CodePosition entry_position = { entry_block, 0 };
}

}

template<std::derived_from<check::Value> T>
struct optional_traits<T> {
    static constexpr T empty_value = T();
};

template<>
struct optional_traits<check::BlockId> {
    static constexpr check::BlockId empty_value = {};
};

static_assert(sizeof(std::optional<check::Value>) == sizeof(check::Value));
static_assert(sizeof(std::optional<check::BooleanValue>) == sizeof(check::BooleanValue));