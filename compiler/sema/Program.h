#pragma once

#include <glue/DeclarationNode.h>
#include <parse/Output.h>
#include <sema/Node.h>

namespace sema {

enum class BuiltinId {
    Type,
    Namespace,
    FunctionId,
};

enum class ValuePhase : uint8_t {
    Value,
    Constant,
    Builtin,
};
struct Value {
    constexpr Value() = default;
    constexpr Value(ValuePhase phase, uint32_t id)
        : idBits(id), phaseBits(std::to_underlying(phase)) { }
    constexpr Value(BuiltinId id)
        : Value(ValuePhase::Builtin, std::to_underlying(id)) { }

    static Value fromUint(uint32_t x) { return std::bit_cast<Value>(x); }
    uint32_t toUint() const { return std::bit_cast<uint32_t>(*this); }

    constexpr uint32_t id() const { return idBits; }
    constexpr ValuePhase phase() const { return (ValuePhase)phaseBits; }

    constexpr bool operator==(const Value& other) const = default;

    uint32_t idBits : 30 = 0;
    uint32_t phaseBits : 2 = 0;
};

struct Type : Value {
    static Type fromUint(uint32_t x) { return Type(Value::fromUint(x)); }
    using Value::Value;
    constexpr explicit Type(Value value)
        : Value(value) { }
};

namespace builtins {
    constexpr inline Type type_type(BuiltinId::Type);
    constexpr inline Type namespace_type(BuiltinId::Namespace);
    constexpr inline Value function_id_template(BuiltinId::FunctionId);
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

struct BasicBlock {
    std::vector<Node> nodes;

    void emitValueExpr(TaggedSourceLocation<NodeKind> location, Value value);
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
        ProgramLiteral,
        DeclarationNodeLiteral,
        StaticAccess,
        Expression,
        ImplicitTemplateParameter,
        ExplicitTemplateParameter,
    };
    struct Constant {
        Opcode op;
        Type type;
        uint64_t data;
    };

    std::vector<Constant> constants;
    BasicBlock expressions;

    Value addLiteral(Type type, glue::DeclarationNode* decl);
    Value addLiteral(Type type, Program* prog);
    Value addExpression(Node* expr);
    Value addImplicitParameter(Type type);
    Value addParameter(Word name, Type type, std::optional<Value> value);

    Type type;
    Value value;

    uint32_t implicitParameterCount = 0;
    uint32_t explicitParameterCount = 0;

    Word name() const { return m_name; }
    ProgramStatus status() const { return m_status; }
    void setStatus(ProgramStatus status) { m_status = status; }
    bool isTemplate() const {
        return implicitParameterCount == 0 && explicitParameterCount == 0;
    }
};

}