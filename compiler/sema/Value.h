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

enum class ValueKind : uint8_t {
    Program, // either not dependent or a template
    Namespace,
    Parameter,
    TemplateSignature$Program,
    TemplateSignature$Parameterize,
    FunctionSignature$Program,
    FunctionSignature$Parameterize,
    Parameterize, // either all argument substituted or just the inherited ones
    Expression,
    RemoteExpression,
    Invalid = 15,
};
struct Value {
    constexpr Value()
        : Value(ValueKind::Invalid, MAX_ID) { }
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
        VERIFY(kind() == ValueKind::Parameter);
        return id();
    }

    constexpr bool operator==(const Value&) const = default;

    static constexpr uint32_t MAX_ID = (1u << 28) - 1u;

    uint32_t idBits : 28;
    uint32_t kindBits : 4;
};
inline constexpr Value INVALID_VALUE = { ValueKind::Invalid, Value::MAX_ID };

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
    static ScopeValue fromUint(uint32_t u) { return ScopeValue(Value::fromUint(u)); }

    ScopeValue() = default;
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
    Value value;
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
    static constexpr sema::ScopeValue empty_value = {};
};