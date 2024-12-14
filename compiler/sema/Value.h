#pragma once

#include <types.h>

#include <utility>

namespace sema {

enum class BuiltinId : uint32_t {

#define BUILTIN(name, cppName) cppName,
#include <sema/builtins.inc>

    COUNT,
};

struct Value;

struct ProgramHandle {
    uint32_t m_id = -1;
    constexpr uint32_t id() const { return m_id; }

    bool operator==(const ProgramHandle&) const = default;
};

struct NamespaceHandle {
    uint32_t m_id = -1;
    constexpr uint32_t id() const { return m_id; }

    bool operator==(const NamespaceHandle&) const = default;
};

inline constexpr int_t VALUE_KIND_BITS = 8;
inline constexpr uint32_t MAX_VALUE_ID = (1u << (32 - VALUE_KIND_BITS)) - 1u;
inline constexpr uint32_t INVALID_VALUE_KIND_INDEX = (1u << VALUE_KIND_BITS) - 1u;

enum class ValueKind : uint8_t {
    // The ordering in this enum determines the ordering of values
    // This ordering needs to be defined between aritatry values to make some data strutures work,
    // but it is most important between values of the same type where it used to orient equalities.

    Program, // either not dependent or a template
    Namespace,
    TemplateSignature$Program,
    TemplateSignature$Parameterize,
    FunctionSignature$Program,
    FunctionSignature$Parameterize,
    BooleanLiteral,
    MemberPointer,

    Parameterize, // either all argument substituted or just the inherited ones
    Expression,
    RemoteExpression,

    CopyOfParameter,

    Invalid = INVALID_VALUE_KIND_INDEX,
};
struct Value {
    constexpr Value(ValueKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) { }
    constexpr explicit Value(ProgramHandle prog)
        : Value(ValueKind::Program, prog.id()) { }
    constexpr explicit Value(NamespaceHandle ns)
        : Value(ValueKind::Namespace, ns.id()) { }
    constexpr Value(BuiltinId id)
        : Value(ProgramHandle(std::to_underlying(id))) { }

    static Value fromUint(uint32_t x) { return std::bit_cast<Value>(x); }
    uint32_t toUint() const { return std::bit_cast<uint32_t>(*this); }

    constexpr uint32_t id() const { return idBits; }
    constexpr ValueKind kind() const { return (ValueKind)kindBits; }

    constexpr ProgramHandle program() const {
        VERIFY(kind() == ValueKind::Program);
        return { id() };
    }
    constexpr NamespaceHandle nsHandle() const {
        VERIFY(kind() == ValueKind::Namespace);
        return { id() };
    }
    constexpr ProgramHandle templateSignatureProgram() const {
        VERIFY(kind() == ValueKind::TemplateSignature$Program);
        return { id() };
    }
    constexpr Value templateSignatureBaseValue() const {
        if (kind() == ValueKind::TemplateSignature$Program)
            return Value(ValueKind::Program, id());
        if (kind() == ValueKind::TemplateSignature$Parameterize)
            return Value(ValueKind::Parameterize, id());
        VERIFY_NOT_REACHED();
    }
    constexpr ProgramHandle functionSignatureProgram() const {
        VERIFY(kind() == ValueKind::FunctionSignature$Program);
        return { id() };
    }
    constexpr Value functionSignatureBaseValue() const {
        if (kind() == ValueKind::FunctionSignature$Program)
            return Value(ValueKind::Program, id());
        if (kind() == ValueKind::FunctionSignature$Parameterize)
            return Value(ValueKind::Parameterize, id());
        VERIFY_NOT_REACHED();
    }
    constexpr int_t expressionIndex() const {
        VERIFY(kind() == ValueKind::Expression);
        return id();
    }
    constexpr int_t parameterIndex() const {
        VERIFY(kind() == ValueKind::CopyOfParameter);
        return id();
    }
    constexpr bool booleanValue() const {
        VERIFY(kind() == ValueKind::BooleanLiteral);
        return idBits != 0;
    }

    constexpr bool operator==(const Value&) const = default;

    uint32_t idBits : (32 - VALUE_KIND_BITS);
    uint32_t kindBits : VALUE_KIND_BITS;
};
inline constexpr Value INVALID_VALUE = { ValueKind::Invalid, MAX_VALUE_ID };

struct Type : Value {
    static Type fromUint(uint32_t x) { return Type(Value::fromUint(x)); }
    using Value::Value;
    constexpr explicit Type(Value value)
        : Value(value) { }
};

struct ExternValue {
    constexpr ExternValue(Value value)
        : value(value) { }

    constexpr uint32_t id() const { return value.id(); }
    constexpr ValueKind kind() const { return value.kind(); }
    constexpr ProgramHandle program() const { return value.program(); }

    constexpr explicit operator Value() const { return value; }

    bool operator==(const ExternValue&) const = default;

private:
    Value value;
};

struct ScopeValue {
    static constexpr ScopeValue invalidValue() { return {}; }
    static ScopeValue fromUint(uint32_t u) { return ScopeValue(Value::fromUint(u)); }

    constexpr explicit ScopeValue(Value value)
        : value(value) {
        VERIFY(value.kind() == ValueKind::Program || value.kind() == ValueKind::Namespace || value.kind() == ValueKind::Invalid);
    }
    constexpr ScopeValue(ProgramHandle prog)
        : value(prog) { }
    constexpr ScopeValue(NamespaceHandle ns)
        : value(ns) { }

    constexpr ValueKind kind() const { return value.kind(); }
    constexpr ProgramHandle program() const { return value.program(); }
    constexpr NamespaceHandle nsHandle() const { return value.nsHandle(); }

    uint32_t toUint() const { return value.toUint(); }

    bool operator==(const ScopeValue&) const = default;

private:
    constexpr ScopeValue()
        : value(INVALID_VALUE) { }

    Value value;
};
inline constexpr ScopeValue INVALID_SCOPE_VALUE = ScopeValue::invalidValue();

enum class ReferenceExpressionKind : uint8_t {
    Parameter,
    TemplateParameter,
    LocalVariable,
    LocalReference,
    MemberExpression,
    OpaqueExpression,
    Invalid = INVALID_VALUE_KIND_INDEX,
};

struct ReferenceExpression {
    static constexpr ReferenceExpression localVariable(uint32_t id) {
        return ReferenceExpression(ReferenceExpressionKind::LocalVariable, id);
    }
    static constexpr ReferenceExpression localReference(uint32_t id) {
        return ReferenceExpression(ReferenceExpressionKind::LocalReference, id);
    }

    constexpr ReferenceExpression(ReferenceExpressionKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) { }

    constexpr ReferenceExpressionKind kind() const { return (ReferenceExpressionKind)kindBits; }
    constexpr int_t id() const { return idBits; }
    constexpr int_t localVaraibleIndex() const {
        VERIFY(kind() == ReferenceExpressionKind::LocalVariable);
        return id();
    }
    constexpr int_t parameterIndex() const {
        VERIFY(kind() == ReferenceExpressionKind::Parameter);
        return id();
    }
    constexpr int_t templateParameterIndex() const {
        VERIFY(kind() == ReferenceExpressionKind::TemplateParameter);
        return id();
    }
    constexpr Value copyTemplateParameter() const {
        VERIFY(kind() == ReferenceExpressionKind::TemplateParameter);
        return Value(ValueKind::CopyOfParameter, id());
    }
    constexpr int_t localReferenceIndex() const {
        VERIFY(kind() == ReferenceExpressionKind::LocalReference);
        return id();
    }
    constexpr int_t opaqueExpressionIndex() const {
        VERIFY(kind() == ReferenceExpressionKind::OpaqueExpression);
        return id();
    }

    constexpr bool operator==(const ReferenceExpression&) const = default;

    uint32_t idBits : (32 - VALUE_KIND_BITS);
    uint32_t kindBits : VALUE_KIND_BITS;
};

inline constexpr ReferenceExpression INVALID_REFERENCE_EXPRESSION = { ReferenceExpressionKind::Invalid, MAX_VALUE_ID };

struct ValueOrReferenceExpression {
    static constexpr ValueOrReferenceExpression invalidValue() { return {}; }

    constexpr ValueOrReferenceExpression(Value value)
        : idBits(value.idBits), kindBits(value.kindBits), isRefBit(0) {
        VERIFY(value.kindBits < INVALID_VALUE_KIND_INDEX / 2);
    }
    constexpr ValueOrReferenceExpression(ReferenceExpression expr)
        : idBits(expr.idBits), kindBits(expr.kindBits), isRefBit(1) {
        VERIFY(expr.kindBits < INVALID_VALUE_KIND_INDEX / 2);
    }
    constexpr bool isReference() const { return isRefBit != 0; }
    constexpr bool isValue() const { return !isReference(); }
    Value value() const {
        VERIFY(isValue());
        return std::bit_cast<Value>(*this);
    }
    constexpr ReferenceExpression reference() const {
        VERIFY(isReference());
        auto tmp = *this;
        tmp.isRefBit = 0;
        return std::bit_cast<ReferenceExpression>(tmp);
    }

    constexpr bool operator==(const ValueOrReferenceExpression&) const = default;

    uint32_t idBits : (32 - VALUE_KIND_BITS);
    uint32_t kindBits : VALUE_KIND_BITS - 1;
    uint32_t isRefBit : 1;

private:
    constexpr ValueOrReferenceExpression()
        : idBits(MAX_VALUE_ID), kindBits(INVALID_VALUE_KIND_INDEX / 2), isRefBit(1) { }
};

}
template<>
struct optional_traits<sema::ProgramHandle> {
    static constexpr sema::ProgramHandle empty_value = {};
};
template<>
struct optional_traits<sema::NamespaceHandle> {
    static constexpr sema::NamespaceHandle empty_value = {};
};
template<>
struct optional_traits<sema::Value> {
    static constexpr sema::Value empty_value = sema::INVALID_VALUE;
};
template<>
struct optional_traits<sema::Type> {
    static constexpr sema::Type empty_value = (sema::Type)sema::INVALID_VALUE;
};
template<>
struct optional_traits<sema::ExternValue> {
    static constexpr sema::ExternValue empty_value = sema::INVALID_VALUE;
};
template<>
struct optional_traits<sema::ScopeValue> {
    static constexpr sema::ScopeValue empty_value = sema::INVALID_SCOPE_VALUE;
};
template<>
struct optional_traits<sema::ReferenceExpression> {
    static constexpr sema::ReferenceExpression empty_value = sema::INVALID_REFERENCE_EXPRESSION;
};
template<>
struct optional_traits<sema::ValueOrReferenceExpression> {
    static constexpr sema::ValueOrReferenceExpression empty_value = sema::ValueOrReferenceExpression::invalidValue();
};