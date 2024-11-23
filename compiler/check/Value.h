#pragma once

#include <types.h>

namespace check {

inline constexpr int_t SOLVER_INTERNAL_VARS_THEORY_ID = 0;
inline constexpr int_t BUILTIN_TYPES_THEORY_ID = 1;

inline constexpr int_t ENTRY_BLOCKS_THEORY_ID = 0;

//! A value
/*!
A value is identified by its theory and its id within that theory.
*/
struct Value {
    uint32_t theoryId : 8 = -1;
    uint32_t valueId : 24 = -1;

    bool operator==(const Value& other) const = default;
};

//! A boolean value
/*!
In the literature on boolean satisfiability this would be called a literal.
For literal X there exist the complementary literal NOT X. These will always belong to the same theory.
\see BooleanTheory::negate()
*/
struct BooleanValue : Value { };

//! A type value
struct Type : Value { };

//! A memory location value
struct MemoryLocation : Value { };

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

    inline constexpr Type type_type = { BUILTIN_TYPES_THEORY_ID, 0 };
    inline constexpr Type boolean_type = { BUILTIN_TYPES_THEORY_ID, 1 };

    inline constexpr BlockId entry_block = { ENTRY_BLOCKS_THEORY_ID, 0 };

    inline constexpr CodePosition entry_position = { entry_block, 0 };
}

}

template<>
struct optional_traits<check::Value> {
    static constexpr check::Value empty_value = check::Value();
};

template<>
struct optional_traits<check::BooleanValue> {
    static constexpr check::BooleanValue empty_value = check::BooleanValue();
};

template<>
struct optional_traits<check::Type> {
    static constexpr check::Type empty_value = check::Type();
};

template<>
struct optional_traits<check::MemoryLocation> {
    static constexpr check::MemoryLocation empty_value = check::MemoryLocation();
};

template<>
struct optional_traits<check::BlockId> {
    static constexpr check::BlockId empty_value = {};
};