#pragma once

#include <parse/Output.h>
#include <sema/Node.h>
#include <sema/Scope.h>

namespace sema {

struct Context;

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
    NodeData data() const { return node()->u; }

    Node* m_node;
};

struct Expression : NodeHandle {
    Expression(Node* node)
        : NodeHandle(node) { VERIFY(isExpression(kind())); }
    NodeCategory category() const { return nodeCategory(kind()); }
    Type type() const { return NodeHandle::data().expr.type; }
    ExprData data() const { return NodeHandle::data().expr.u; }
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

    Program(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeValue parent, SourceLocation location)
        : m_fields(Fields(kind), location)
        , m_name(name)
        , m_parent(parent)
        , parseLocation(parseLocation) { }

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
        // TODO: Returning a span referencing valueData may be a bad idea
        VERIFY(value.kind() == ValueKind::Parameterize);
        Value* begin = &valueData[value.id()];
        int_t argumentCount = begin[0].id();
        return Parameterize { begin[1].program(), { begin + 2, (size_t)argumentCount } };
    }

    SourceLocation declarationLocation() const { return m_fields.location(); }

    void setType(Type type) {
        VERIFY(!m_type.has_value());
        m_type = type;
    }

    ScopeValue parent() const {
        return m_parent;
    }

    Word name() const { return m_name; }
    ProgramStatus status() const { return m_fields.tag().status(); }
    ProgramKind kind() const { return m_fields.tag().kind(); }
    bool isDependent() const {
        return !parameters.empty();
    }
    bool isTemplate() const {
        return parameters.size() > inheritedParameterCount;
    }

    void dump(Context&);
    ChildrenRange topLevelNodes() {
        return ChildrenRange(expressions.data() + expressions.size(), expressions.size() + 1);
    }
    DataValueRange dataValues() {
        return DataValueRange { valueData };
    }

    parse::TokenHandle beginSignatureCheck() {
        VERIFY(status() == ProgramStatus::Unchecked);
        auto tag = m_fields.tag();
        tag.setStatus(ProgramStatus::SignatureCheckInProgress);
        m_fields.setTag(tag);
        return parseLocation;
    }

    void completeSignatureCheck() {
        VERIFY(status() == ProgramStatus::SignatureCheckInProgress);
        auto tag = m_fields.tag();
        tag.setStatus(ProgramStatus::SignatureChecked);
        m_fields.setTag(tag);
    }

    ProgramHandle translate(ProgramHandle handle) const {
        return programTranslationBuffer[handle.id()];
    }
    NamespaceHandle translate(NamespaceHandle handle) const {
        return namespaceTranslationBuffer[handle.id()];
    }
    ScopeValue translate(ScopeValue value) const {
        if (value.kind() == ValueKind::Program)
            return translate(value.program());
        if (value.kind() == ValueKind::Namespace)
            return translate(value.nsHandle());
        return value;
    }

public:
    struct Fields {
        uint8_t kindBits : 2;
        uint8_t statusBits : 3;

        Fields(ProgramKind kind)
            : kindBits(std::to_underlying(kind))
            , statusBits(std::to_underlying(ProgramStatus::Unchecked)) { }

        void setStatus(ProgramStatus status) { statusBits = std::to_underlying(status); }
        ProgramStatus status() const { return (ProgramStatus)statusBits; }
        ProgramKind kind() const { return (ProgramKind)kindBits; }
    };
    TaggedSourceLocation<Fields> m_fields;
    uint32_t inheritedParameterCount = 0;
    Word m_name;

    std::vector<Parameter> parameters;
    std::vector<Node> expressions;
    std::vector<Value> valueData;

protected:
    static constexpr uint32_t INVALID_SUBCLASS_DATA = -1;

    std::optional<ExternValue> m_type;
    uint32_t m_subClassData = INVALID_SUBCLASS_DATA;
    ScopeValue m_parent;
    parse::TokenHandle parseLocation;

    const ProgramHandle* programTranslationBuffer = nullptr;
    const NamespaceHandle* namespaceTranslationBuffer = nullptr;

    friend struct Dumper;
    friend Context; // set translation buffers
};
static_assert(sizeof(Program) == 120);

struct ValueProgram : Program {
    ValueProgram(Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location)
        : Program(ProgramKind::Value, name, parseLocation, rawParent, location) { }

    void setValue(Value value) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = value.toUint();
    }

    ExternValue value() const {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
        return Value::fromUint(m_subClassData);
    }
    ExternValue type() const { return m_type.value(); }
};

enum class RuntimeParameterKind : uint8_t {
    UncheckedMember,
    Member,
    HasMember,
    LetParameter,
    VarParameter,
    InParameter,
    InOutParameter,
    OutParameter,
};

struct RuntimeParameter {
    TaggedSourceLocation<RuntimeParameterKind> m_location;
    Word name;
    union {
        parse::TokenHandle parseLocation; // active for unchecked kinds
        Type type;
    } u;

    RuntimeParameter(RuntimeParameterKind kind, Word name, Type type, SourceLocation location)
        : m_location(kind, location), name(name), u { .type = type } { }

    RuntimeParameter(Word name, parse::TokenHandle parseLocation, SourceLocation location)
        : m_location(RuntimeParameterKind::UncheckedMember, location)
        , name(name)
        , u { .parseLocation = parseLocation } { }

    void setKind(RuntimeParameterKind kind) {
        m_location.setTag(kind);
    }

    RuntimeParameterKind kind() const { return m_location.tag(); }
    SourceLocation location() const { return m_location.location(); }
    Type type() const {
        VERIFY(kind() != RuntimeParameterKind::UncheckedMember);
        return u.type;
    }
};

struct CallableProgram : Program {
    CallableProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location)
        : Program(kind, name, parseLocation, rawParent, location) { }

    std::vector<RuntimeParameter> runtimeParameters;
    ExternValue returnType() const { return m_type.value(); }
};

struct FunctionProgram : CallableProgram {
    FunctionProgram(Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location)
        : CallableProgram(ProgramKind::Function, name, parseLocation, rawParent, location) { }

    void setBody(Node* node) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = importNode(node);
    }
    Node* body() {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
        return &expressions[m_subClassData];
    }
};

struct TypeProgram : CallableProgram, Scope {
    TypeProgram(Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location)
        : CallableProgram(ProgramKind::Type, name, parseLocation, rawParent, location) { }
};

union ProgramUnion {
    ValueProgram value;
    FunctionProgram function;
    TypeProgram type;

    ProgramUnion(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location) {
        switch (kind) {
        case ProgramKind::Value:
            std::construct_at(&value, name, parseLocation, rawParent, location);
            break;
        case ProgramKind::Function:
            std::construct_at(&function, name, parseLocation, rawParent, location);
            break;
        case ProgramKind::Type:
            std::construct_at(&type, name, parseLocation, rawParent, location);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
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
static_assert(sizeof(ProgramUnion) == 160);

}