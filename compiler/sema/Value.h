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

enum class ValueKind : uint8_t {
    Program, // either not dependent or templated
    Constant,
    Parameter,
};
struct Value {
    constexpr Value()
        : Value(BuiltinId::error_type) { }
    constexpr Value(ValueKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) { }
    constexpr Value(BuiltinId id)
        : Value(ValueKind::Program, std::to_underlying(id)) { }

    static Value fromUint(uint32_t x) { return std::bit_cast<Value>(x); }
    uint32_t toUint() const { return std::bit_cast<uint32_t>(*this); }

    constexpr uint32_t id() const { return idBits; }
    constexpr ValueKind kind() const { return (ValueKind)kindBits; }

    constexpr ProgramHandle program() const {
        VERIFY(kind() == ValueKind::Program);
        return { id() };
    }

    constexpr bool operator==(const Value&) const = default;

    static constexpr uint32_t MAX_ID = (1 << 30) - 1;

    uint32_t idBits : 30;
    uint32_t kindBits : 2;
};
inline constexpr Value INVALID_VALUE = { (ValueKind)3, Value::MAX_ID };

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
}
template<>
struct optional_traits<sema::ProgramHandle> {
    static constexpr sema::ProgramHandle empty_value = {};
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