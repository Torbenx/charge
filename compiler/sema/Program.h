#pragma once

#include <glue/DeclarationNode.h>
#include <parse/Output.h>
#include <sema/Node.h>
#include <sema/Value.h>

namespace glue {

struct Context;

}

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
    int_t childrenCount() const { return node()->childrenCount(); }
    ChildrenRange reverseChildren() const { return node()->reverseChildren(); }

    Node* m_node;
};

struct Expression : NodeHandle {
    Expression(Node* node)
        : NodeHandle(node) { VERIFY(isExpression(kind())); }
    NodeCategory category() const { return nodeCategory(kind()); }
    Type type() const { return node()->u.expr.type; }
    ExprData data() const { return node()->u.expr.u; }
};

struct Parameterize {
    ProgramHandle base;
    std::span<Value> arguments;
};
struct RemoteExpression {
    Value base;
    uint32_t expressionIndex;
};

enum class ProgramStatus : uint8_t {
    Unchecked,
    SignatureCheckInProgress,
    SignatureChecked, // (template) parameters have been checked, the type has been determined
};

enum class ProgramKind : uint8_t {
    Value,
    Type,
    Function,
};

struct DataValueIterator {
    using value_type = Value;
    using difference_type = int_t;

    DataValueIterator() = default;
    DataValueIterator(Value* begin, Value* position)
        : m_begin(begin), m_pos(position) { }
    DataValueIterator(const DataValueIterator&) = default;
    DataValueIterator(DataValueIterator&&) = default;
    DataValueIterator& operator=(const DataValueIterator&) = default;
    DataValueIterator& operator=(DataValueIterator&&) = default;

    Value getValue() const {
        return Value(m_pos->kind(), m_pos - m_begin);
    }
    DataValueIterator& operator++() {
        advance();
        return *this;
    }
    DataValueIterator operator++(int) {
        DataValueIterator copy = *this;
        advance();
        return copy;
    }
    Value operator*() const { return getValue(); }

    auto operator<=>(const DataValueIterator& other) const {
        VERIFY(other.m_begin == m_begin);
        return m_pos <=> other.m_pos;
    }
    bool operator==(const DataValueIterator&) const = default;

private:
    void advance() {
        switch (m_pos->kind()) {
        case ValueKind::Parameterize:
            m_pos += 2 + m_pos->id();
            break;
        case ValueKind::RemoteExpression:
            m_pos += 2;
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
    Value* m_begin = nullptr;
    Value* m_pos = nullptr;
};

struct DataValueRange {
    DataValueIterator begin() const {
        return { values.data(), values.data() };
    }
    DataValueIterator end() const {
        return { values.data(), values.data() + values.size() };
    }

    std::span<Value> values;
};

struct Program {
    struct Parameter {
        Word name;
        ExternValue type;
        std::optional<Value> defaultValue;

        bool implicit() const { return name.empty(); }
    };

    int_t importNode(Node* node);
    Value addExpression(Node* expr);
    Value addRemoteExpression(Value base, uint32_t expressionIndex);
    Value addParameterize(ProgramHandle base, std::span<const Value> arguments);
    RemoteExpression getRemoteExpression(ExternValue value) {
        VERIFY(value.kind() == ValueKind::RemoteExpression);
        Value* begin = &valueData[value.id()];
        return RemoteExpression { begin[1], begin[0].id() };
    }
    Parameterize getParameterize(ExternValue value) {
        // TODO: Returning a span referencing valueData maybe a bad idea
        VERIFY(value.kind() == ValueKind::Parameterize);
        Value* begin = &valueData[value.id()];
        int_t argumentCount = begin[0].id();
        return Parameterize { begin[1].program(), { begin + 2, (size_t)argumentCount } };
    }

    // type after substituitng template arguments
    ExternValue type() const { return m_type.value(); }

    ExternValue parent() const { return m_parent.value(); }

    ExternValue self() const { return m_self.value(); }

    void setType(Type type) {
        VERIFY(!m_type.has_value());
        m_type = type;
    }
    void setParent(Value value) {
        VERIFY(!m_parent.has_value());
        m_parent = value;
    }
    void setSelf(Value value) {
        VERIFY(!m_self.has_value());
        m_self = value;
    }

    Word name() const { return m_name; }
    ProgramStatus status() const { return m_status; }
    void setStatus(ProgramStatus status) { m_status = status; }
    ProgramKind kind() const { return m_kind; }
    bool isDependent() const {
        return !parameters.empty();
    }
    bool isTemplate() const {
        return parameters.size() > inheritedParameterCount;
    }

    void dump(glue::Context&);
    ChildrenRange topLevelNodes() {
        return ChildrenRange(expressions.data() + expressions.size(), expressions.size() + 1);
    }
    DataValueRange dataValues() {
        return DataValueRange { valueData };
    }

public:
    ProgramStatus m_status = ProgramStatus::Unchecked;
    ProgramKind m_kind = ProgramKind::Value;
    uint16_t inheritedParameterCount = 0;
    Word m_name;

    std::vector<Parameter> parameters;
    std::vector<Node> expressions;
    std::vector<Value> valueData;

    const ProgramHandle* programTranslationBuffer = nullptr;

protected:
    static constexpr uint32_t INVALID_SUBCLASS_DATA = -1;

    std::optional<ExternValue> m_type;
    uint32_t m_subClassData = INVALID_SUBCLASS_DATA;
    std::optional<ExternValue> m_parent;
    std::optional<ExternValue> m_self;

    friend struct Dumper;
};
static_assert(sizeof(Program) == 104);

struct ValueProgram : Program {
    void setValue(Value value) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = value.toUint();
    }

    ExternValue value() const {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
        return Value::fromUint(m_subClassData);
    }
};

struct FunctionProgram : Program {
    struct FunctionParameter {
        Word name;
        Type type;
    };

    void setBody(Node* node) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = importNode(node);
    }
    Node* body() {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
        return &expressions[m_subClassData];
    }

    std::vector<FunctionParameter> functionParameters;
};

struct TypeProgram : Program {
    struct Member { };
    std::vector<Member> members;
};

union ProgramUnion {
    ValueProgram value;
    FunctionProgram function;
    TypeProgram type;

    ProgramUnion(ProgramKind kind) {
        switch (kind) {
        case ProgramKind::Value:
            std::construct_at(&value);
            break;
        case ProgramKind::Function:
            std::construct_at(&function);
            break;
        case ProgramKind::Type:
            std::construct_at(&type);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        value.m_kind = kind;
    }

    Program& get() { return value; }

    ~ProgramUnion() {
        switch (value.kind()) {
        case ProgramKind::Value:
            std::destroy_at(&value);
            break;
        case ProgramKind::Function:
            std::destroy_at(&function);
            break;
        case ProgramKind::Type:
            std::destroy_at(&type);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
};
static_assert(sizeof(ProgramUnion) == 128);

}