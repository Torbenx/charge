#pragma once

#include <check/Value.h>

namespace check {

struct Solver;

struct ValueTheory {
    ValueTheory(Solver& solver, ValueKind valuesKind);
    virtual ~ValueTheory() = default;
    ValueTheory(const ValueTheory&) = delete;
    ValueTheory(ValueTheory&&) = delete;
    ValueTheory& operator=(const ValueTheory&) = delete;
    ValueTheory& operator=(ValueTheory&&) = delete;

    virtual uint64_t labelOfValue(Solver&, Value) = 0;
    virtual std::string formatValue(Solver&, Value) = 0;

    //! Collect a set of literals that imply that the value is inactive
    virtual void collectValueInactiveReasons(Solver&, Value, std::vector<BooleanValue>&) { }

    //! Must be return true if and only if all literals added by collectValueInactiveReasons for the value are false
    virtual bool isValueActive(Solver&, Value) { return true; }

    virtual void enumerateValues(Solver&, std::function<void(Value)> visitor) = 0;

    int_t theoryId() const { return m_theoryId; }
    ValueKind valuesKind() const { return m_valuesKind; }

private:
    uint8_t m_theoryId;
    ValueKind m_valuesKind;
};

struct EquatableValueTheory : ValueTheory {
    struct EqualityInfo;

    using ValueTheory::ValueTheory;

    virtual EqualityInfo& equalityInfo(Solver&, Value) = 0;
};

struct BooleanTheory : ValueTheory {
    struct LiteralInfo;

    BooleanTheory(Solver& solver)
        : ValueTheory(solver, ValueKind::Boolean) { }

    virtual BooleanValue negate(Solver&, BooleanValue) = 0;
    virtual LiteralInfo& literalInfo(Solver&, BooleanValue) = 0;
    virtual void propagateAssignment(Solver&, BooleanValue) = 0;

    //! Reapply an assignment that was reverted during ReasonTheory::backtrack()
    /*!
    Should only be used when a reason theory also uses backtrack.
    */
    virtual void reapplyAssignment(Solver&, BooleanValue) = 0;

    //! Revert an assignment that was not reverted during ReasonTheory::backtrack()
    /*!
    Should not be used when a reason theory also reverts assignments in backtrack.
    */
    virtual void unapplyAssignment(Solver&, BooleanValue) = 0;
};

struct MemberExpressionTheory : EquatableValueTheory {
    struct LiteralInfo {
        Type baseType;
    };

    MemberExpressionTheory(Solver& solver)
        : EquatableValueTheory(solver, ValueKind::MemberExpression) { }

    // Note: We don't have baseType() function because that would require introducing
    //       a type variable for each memory location without a known declaration.
    virtual Type memberType(Solver&, MemberExpression) = 0;

    virtual std::optional<LiteralInfo> literalInfo(Solver&, MemberExpression) = 0;
};

struct MemoryDeclarationTheory : EquatableValueTheory {
    struct DeclarationInfo {
        Type type;
        CodePosition position;
    };

    MemoryDeclarationTheory(Solver& solver)
        : EquatableValueTheory(solver, ValueKind::MemoryDeclaration) { }

    //! Returns info for an object declaration
    /*!
    For concrete declarations this should return a value and for unknows/variables this should
    return an empty optional. If this returns a value for two values the values are always
    disequal.
    */
    virtual std::optional<DeclarationInfo> declarationInfo(Solver&, MemoryDeclaration) = 0;
};

struct MemoryLocationTheory : ValueTheory {
    MemoryLocationTheory(Solver& solver)
        : ValueTheory(solver, ValueKind::MemoryLocation) { }

    virtual Type typeAtLocation(Solver&, MemoryLocation) = 0;

    virtual MemoryDeclaration memoryDeclaration(Solver&, MemoryLocation) = 0;
    virtual MemberExpression memberExpression(Solver&, MemoryLocation) = 0;
};

struct TypeTheory : EquatableValueTheory {
    TypeTheory(Solver& solver)
        : EquatableValueTheory(solver, ValueKind::Type) { }

    //! Return the scalar value kind to use for a value of the given type
    virtual std::optional<ValueKind> scalarKind(Solver&, Type) = 0;

    virtual std::optional<Type> dereferencedType(Solver&, Type) = 0;
    virtual std::optional<Type> memberExpressionMemberType(Solver&, Type) = 0;
    virtual std::optional<Type> memberExpressionBaseType(Solver&, Type) = 0;
};

struct ValueKindTheory {
    virtual std::string formatValueKind(Solver&, ValueKind) = 0;
    virtual BooleanValue equality(Solver&, Value, Value) = 0;
    virtual BooleanValue disequality(Solver&, Value, Value) = 0;
    virtual Value defineLoad(Solver&, MemoryLocation, CodePosition) = 0;
    virtual ~ValueKindTheory() { }
};

}