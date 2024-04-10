#pragma once

#include <glue/DeclarationNode.h>
#include <parse/Output.h>
#include <sema/Node.h>

namespace sema {

enum class BuiltinId {

#define BUILTIN(name, cppName) cppName,
#include <sema/builtins.inc>

    COUNT,
};

enum class ValueKind : uint8_t {
    Builtin,
    Constant,
    Local,
};
struct Value {
    constexpr Value()
        : Value(BuiltinId::error_type) { }
    constexpr Value(ValueKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) { }
    constexpr Value(BuiltinId id)
        : Value(ValueKind::Builtin, std::to_underlying(id)) { }

    static Value fromUint(uint32_t x) { return std::bit_cast<Value>(x); }
    uint32_t toUint() const { return std::bit_cast<uint32_t>(*this); }

    constexpr uint32_t id() const { return idBits; }
    constexpr ValueKind kind() const { return (ValueKind)kindBits; }

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

    constexpr explicit operator Value() const { return value; }

    bool operator==(const ExternValue&) const = default;

private:
    Value value;
};
}
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

namespace sema {

namespace builtins {

#define BUILTIN_TYPE(name) constexpr inline Type name##_type { BuiltinId::name##_type };
#define BUILTIN(name, cppName) constexpr inline Value cppName { BuiltinId::cppName };
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

#define ENUMERATE_PROGRAM_OPS    \
    PROGRAM_OP(ProgramLiteral)   \
    PROGRAM_OP(SignatureOf)      \
    PROGRAM_OP(NamespaceLiteral) \
    PROGRAM_OP(RemoteExpression) \
    PROGRAM_OP(Expression)       \
    PROGRAM_OP(Parameter)        \
    PROGRAM_OP(Parameterize)

struct Program {
    ProgramStatus m_status = ProgramStatus::Unchecked;
    Word m_name;

    enum class Opcode : uint8_t {
#define PROGRAM_OP(kind) kind,
        ENUMERATE_PROGRAM_OPS
#undef PROGRAM_OP
    };
    struct Constant {
        Opcode op;
        Type type;
        union {
            uint64_t data;
            Program* program;
            glue::DeclarationNode* declarationNode;
            uint32_t parameterIndex;
            uint32_t expressionIndex; // offset into expressions
            struct {
                Value base; // either a non-dependent program literal or parameterize
                uint32_t expressionIndex; // offset into the target programs expressions
            } remoteExpression;
            struct {
                Value base; // always a dependent program literal
                uint16_t firstArgumentIndex; // offset into parameterizeArguments
                uint16_t argumentCount;
            } parameterize;
        } u;
    };
    static_assert(sizeof(Constant) == 16);

    struct Parameter {
        Word name;
        Value parameterValue;
        std::optional<Value> defaultValue;
    };

    struct ParameterizeArgumentSetter {
        Program* program = nullptr;
        int_t firstIndex = 0;

        void set(int_t index, Value value) {
            program->parameterizeArguments[firstIndex + index] = value;
        }
    };

    std::vector<Constant> constants;
    std::vector<Node> expressions;
    std::vector<Value> parameterizeArguments;

    ExternValue typeOf(ExternValue v) {
        VERIFY(v.kind() == ValueKind::Constant);
        return constants[v.id()].type;
    }

    Value add(Constant);
    Value addParameter(Word name, Type type, std::optional<Value> defaultValue);
    Value addNamespaceLiteral(glue::DeclarationNode* decl);
    Value addProgramLiteral(Opcode op, Type type, Program* prog);
    Value addExpression(Node* expr);
    Value addExplicitParameter(Word name, Type type, std::optional<Value> defaultValue);
    Value addImplicitParameter(Type type);
    Value addInheritedParameter(Type type, std::optional<Value> defaultValue);
    Value addRemoteExpression(Type type, Value base, uint32_t expressionIndex);
    Value addParameterize(Type type, Value base, int_t firstArgumentIndex, int_t argumentCount);
    std::pair<Value, ParameterizeArgumentSetter> addParameterize(Type type, Value base, int_t argumentCount);

    // type after substituitng template arguments
    ExternValue type() const { return m_type.value(); }

    // value after substituitng template arguments
    ExternValue value() const { return m_value.value(); }

    Value parameterValue(int_t index) const { return parameters[index].parameterValue; }

    void setType(Type type) {
        m_type = type;
    }
    void setValue(Value value) {
        m_value = value;
    }

    std::optional<ExternValue> m_type;
    std::optional<ExternValue> m_value;

    std::vector<Parameter> parameters;
    uint32_t inheritedParameterCount = 0;
    uint32_t implicitParameterCount = 0;

    Word name() const { return m_name; }
    ProgramStatus status() const { return m_status; }
    void setStatus(ProgramStatus status) { m_status = status; }
    bool isDependent() const {
        return !parameters.empty();
    }
    bool isTemplate() const {
        return parameters.size() > inheritedParameterCount;
    }

    void dump();
};

std::string_view nameString(Program::Opcode op);

extern std::array<Program, std::to_underlying(BuiltinId::COUNT)> builtinPrograms;

}