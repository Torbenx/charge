#pragma once

#include <glue/DeclarationNode.h>
#include <parse/Output.h>
#include <sema/Node.h>

namespace sema {

enum class BuiltinId {

#define BUILTIN(name) name,
#include <sema/builtins.inc>

    COUNT,
};

enum class ValueKind : uint8_t {
    Builtin,
    Local,
    Constant,
};
struct Value {
    constexpr Value()
        : Value(BuiltinId::error_value) { }
    constexpr Value(ValueKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) { }
    constexpr Value(BuiltinId id)
        : Value(ValueKind::Builtin, std::to_underlying(id)) { }

    static Value fromUint(uint32_t x) { return std::bit_cast<Value>(x); }
    uint32_t toUint() const { return std::bit_cast<uint32_t>(*this); }

    constexpr uint32_t id() const { return idBits; }
    constexpr ValueKind kind() const { return (ValueKind)kindBits; }

    constexpr bool operator==(const Value&) const = default;

    uint32_t idBits : 30;
    uint32_t kindBits : 2;
};

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

    constexpr explicit operator Value() const { return value; }

    bool operator==(const ExternValue&) const = default;

private:
    Value value;
};
}
template<>
struct optional_traits<sema::Value> {
    static constexpr sema::Value empty_value = {};
};
template<>
struct optional_traits<sema::Type> {
    static constexpr sema::Type empty_value = {};
};
template<>
struct optional_traits<sema::ExternValue> {
    static constexpr sema::ExternValue empty_value = sema::Value();
};

namespace sema {

namespace builtins {

#define BUILTIN_TYPE(name) constexpr inline Type name { BuiltinId::name };
#define BUILTIN(name) constexpr inline Value name { BuiltinId::name };
#include <sema/builtins.inc>

};

struct NodeHandle {
    NodeHandle(Node* node)
        : m_node(node) { VERIFY(node != nullptr); }

    Node* node() const { return m_node; }
    Node* operator->() const { return node(); }
    operator Node*() const { return node(); }
    NodeKind kind() const { return node()->kind(); }
    SourceLocation location() const { return node()->location(); }
    bool primary() const { return node()->primary(); }
    int_t childrenCount() const { return node()->childrenCount(); }
    ChildrenRange reverseChildren() const { return node()->reverseChildren(); }

    Node* m_node;
};

struct Expression : NodeHandle {
    Expression(Node* node)
        : NodeHandle(node) { VERIFY(isExpression(kind())); }
    NodeCategory category() const { return nodeCategory(kind()); }
    Type type() const { return Type::fromUint(node()->data1); }
};

enum class ProgramStatus : uint8_t {
    Unchecked,
    SignatureCheckInProgress,
    SignatureChecked, // (template) parameters have been checked, the type has been determined
};

struct Program {
    ProgramStatus m_status = ProgramStatus::Unchecked;
    Word m_name;

    enum class Opcode : uint8_t {
        SignatureOf,
        TypeOf,
        ProgramLiteral,
        DeclarationNodeLiteral,
        StaticAccess,
        Expression,
        ImplicitParameter,
        ExplicitParameter,
    };
    struct Constant {
        Opcode op;
        Type type;
        union {
            uint64_t data;
            Program* program;
            glue::DeclarationNode* declarationNode;
            struct {
                uint32_t index;
                std::optional<Value> defaultValue;
            } parameter;
            uint32_t expressionIndex;
            struct {
                Value base;
                uint32_t constantId;
            } access;
        } u;
    };
    static_assert(sizeof(Constant) == 16);

    struct ExplicitParameter {
        Word name;
        ExternValue value;
    };

    std::vector<Constant> constants;
    std::vector<Node> expressions;

    Value add(Constant);
    Value addLiteral(Type type, glue::DeclarationNode* decl);
    Value addProgramLiteral(Opcode op, Type type, Program* prog);
    Value addExpression(Node* expr);
    Value addImplicitParameter(Type type);
    Value addExplicitParameter(Word name, Type type, std::optional<Value> value);
    Value addStaticAccess(Type type, Value base, ExternValue value);

    Program* GetProgramLiteral(ExternValue value);

    ExternValue typeOf(ExternValue value) {
        VERIFY(value.kind() == ValueKind::Constant);
        return constants[value.id()].type;
    }

    // type after substituitng template arguments
    ExternValue type() const { return m_type.value(); }

    // value after substituitng template arguments
    ExternValue value() const { return m_value.value(); }

    void setType(Type type) {
        m_type = type;
    }
    void setValue(Value value) {
        m_value = value;
    }

    std::optional<ExternValue> m_type;
    std::optional<ExternValue> m_value;

    std::vector<ExplicitParameter> explicitParameters;
    std::vector<ExternValue> implicitParameters;
    struct DependentParent {
        ExternValue parent;
        ExternValue parentParameter;
    };
    std::optional<DependentParent> dependentParent;

    Word name() const { return m_name; }
    ProgramStatus status() const { return m_status; }
    void setStatus(ProgramStatus status) { m_status = status; }
    bool isTemplate() const {
        return !explicitParameters.empty() || !implicitParameters.empty();
    }
    bool hasDependentParent() const {
        return dependentParent.has_value();
    }
    bool isDependent() const {
        return isTemplate() || hasDependentParent();
    }
};

extern std::array<Program, std::to_underlying(BuiltinId::COUNT)> builtinPrograms;

}