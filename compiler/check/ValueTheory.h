#pragma once

#include <check/Value.h>

namespace check {

struct Solver;

struct ValueTheory {
    ValueTheory(Solver& solver);
    virtual ~ValueTheory() = default;
    ValueTheory(const ValueTheory&) = delete;
    ValueTheory(ValueTheory&&) = delete;
    ValueTheory& operator=(const ValueTheory&) = delete;
    ValueTheory& operator=(ValueTheory&&) = delete;

    virtual Type typeOf(Solver&, Value) = 0;
    virtual uint64_t labelOf(Solver&, Value) = 0;

    virtual std::string formatValue(Solver&, Value) = 0;

    virtual void enumerateValues(Solver&, std::function<void(Value)> visitor) = 0;

    int_t theoryId() const { return m_theoryId; }

private:
    uint8_t m_theoryId;
};

struct EquatableValueTheory : ValueTheory {
    struct EqualityInfo;

    using ValueTheory::ValueTheory;

    virtual EqualityInfo& equalityInfo(Solver&, Value) = 0;
    virtual void propagateEquality(Solver&, Value source, Value target) = 0;
};

struct BooleanTheory : ValueTheory {
    struct LiteralInfo;

    using ValueTheory::ValueTheory;

    Type typeOf(Solver&, Value) override { return builtins::boolean_type; }

    virtual BooleanValue negate(Solver&, BooleanValue) = 0;
    virtual LiteralInfo& literalInfo(Solver&, BooleanValue) = 0;
    virtual void propagateFalseAssignment(Solver&, BooleanValue) = 0;

    //! Reapply an assignment that was reverted during ReasonTheory::backtrack()
    /*!
    Should only be used when a reason theory also uses backtrack.
    */
    virtual void reapplyFalseAssignment(Solver&, BooleanValue) = 0;

    //! Revert an assignment that was not reverted during ReasonTheory::backtrack()
    /*!
    Should not be used when a reason theory also reverts assignments in backtrack.
    */
    virtual void unapplyFalseAssignment(Solver&, BooleanValue) = 0;
};

struct TypedOperations {
    virtual BooleanValue equality(Solver&, Value, Value) = 0;
    virtual BooleanValue disequality(Solver&, Value, Value) = 0;
    virtual Value defineLoad(Solver&, MemoryLocation, CodePosition) = 0;
};
struct TypeTheory : ValueTheory {
    using ValueTheory::ValueTheory;

    Type typeOf(Solver&, Value) override { return builtins::type_type; }

    virtual TypedOperations& operationsFor(Solver&, Type) = 0;
};

struct MemoryLocationTheory : ValueTheory {
    using ValueTheory::ValueTheory;

    //! Type of a load from the location
    virtual Type loadedType(Solver&, MemoryLocation) = 0;
};

}