#pragma once

#include <types.h>

namespace check {

inline constexpr int_t SOLVER_INTERNAL_VARS_THEORY_ID = 0;
inline constexpr int_t TYPE_THEORY_ID = 1;

//! A value
/*!
A value is identified by its theory and its id within that theory.
*/
struct Value {
    uint32_t theoryId : 8 = -1;
    uint32_t valueId : 24 = -1;

    auto operator<=>(const Value& other) const {
        return std::pair<uint32_t, uint32_t>(theoryId, valueId)
            <=> std::pair<uint32_t, uint32_t>(other.theoryId, other.valueId);
    }
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

namespace builtins {
    inline constexpr BooleanValue true_literal = { SOLVER_INTERNAL_VARS_THEORY_ID, 0 };
    inline constexpr BooleanValue false_literal = { SOLVER_INTERNAL_VARS_THEORY_ID, 1 };

    inline constexpr Type type_type = { TYPE_THEORY_ID, 0 };
    inline constexpr Type boolean_type = { TYPE_THEORY_ID, 1 };
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