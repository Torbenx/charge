#pragma once

#include <FlatTreeSet.h>
#include <parse/Output.h>
#include <sema/Instruction.h>
#include <sema/Scope.h>


namespace sema {

struct Context;
struct RuntimeParameter;

namespace builtins {

#define BUILTIN_TYPE(name) constexpr inline Type name##_type { BuiltinId::name##_type };
#define BUILTIN(name, cppName) constexpr inline Value cppName { BuiltinId::cppName };
#include <sema/builtins.inc>

};

struct Expression {
    Instruction* inst;
    int_t size;

    Expression(Instruction* inst, int_t size)
        : inst(inst), size(size) {
        VERIFY(isExpression(inst->opcode()));
    }

    Opcode opcode() const { return inst->opcode(); }
    InstructionCategory category() const { return categoryOf(opcode()); }
    ExpressionData data() const { return inst->u.expr.u; }
    Type type() const { return inst->u.expr.type; }

    std::span<Instruction> span() const { return { inst - (size - 1), (size_t)size }; }

    Instruction* begin() const { return inst - (size - 1); }
    Instruction* end() const { return inst + 1; }
};

struct ParameterizeData {
    ProgramHandle base;
    std::vector<Value> arguments;
};
struct Parameterize {
    ProgramHandle base;
    std::span<const Value> arguments;

    static Parameterize fromData(const ParameterizeData& data) {
        return { data.base, { data.arguments.data(), data.arguments.size() } };
    }
};
struct ParameterizeSet : FlatTreeSetDetail::Base<ParameterizeSet, ParameterizeData> {
    uint32_t get(Context& context, ProgramHandle prog, Parameterize);

private:
    friend Base;
    uint32_t makeNode(Context&, ProgramHandle, Parameterize, TreeLabel);
    std::strong_ordering compare(Context&, ProgramHandle, Parameterize, ParameterizeData&);
};

struct RemoteExpression {
    Value base;
    ExternValue expression;
};
struct RemoteExpressionSet : FlatTreeSetDetail::Base<RemoteExpressionSet, RemoteExpression> {
    uint32_t get(Context& context, ProgramHandle prog, RemoteExpression);

private:
    friend Base;
    uint32_t makeNode(Context&, ProgramHandle, RemoteExpression, TreeLabel);
    std::strong_ordering compare(Context&, ProgramHandle, RemoteExpression, RemoteExpression);
};

struct MemberPointer {
    Type parentType; // always non-dependent
    uint32_t memberIndex;
};
struct MemberPointerSet : FlatTreeSetDetail::Base<MemberPointerSet, MemberPointer> {
    uint32_t get(Context& context, ProgramHandle prog, MemberPointer);

private:
    friend Base;
    uint32_t makeNode(Context&, ProgramHandle, MemberPointer, TreeLabel);
    std::strong_ordering compare(Context&, ProgramHandle, MemberPointer, MemberPointer);
};

enum class ProgramStatus : uint8_t {
    Unchecked,
    SignatureCheckInProgress,
    SignatureChecked,
};

enum class ProgramKind : uint8_t {
    Value,
    Object,
    Type,
    Function,
};

struct ValueIdIterator {
    using value_type = Value;
    using difference_type = int_t;

    ValueIdIterator() = default;
    explicit ValueIdIterator(Value value)
        : m_value(value) { }
    ValueIdIterator(const ValueIdIterator&) = default;
    ValueIdIterator(ValueIdIterator&&) = default;
    ValueIdIterator& operator=(const ValueIdIterator&) = default;
    ValueIdIterator& operator=(ValueIdIterator&&) = default;

    Value value() const {
        return m_value;
    }
    ValueIdIterator& operator++() {
        advance();
        return *this;
    }
    ValueIdIterator operator++(int) {
        ValueIdIterator copy = *this;
        advance();
        return copy;
    }
    Value operator*() const { return value(); }

    auto operator<=>(const ValueIdIterator& other) const {
        VERIFY(m_value.kind() == other.m_value.kind());
        return m_value.id() <=> other.m_value.id();
    }
    bool operator==(const ValueIdIterator&) const = default;

private:
    void advance() {
        m_value = Value(m_value.kind(), m_value.id() + 1);
    }
    Value m_value;
};
static_assert(std::forward_iterator<ValueIdIterator>);

struct ValueIdRange {
    ValueIdIterator begin() const {
        return ValueIdIterator(Value(endValue.kind(), 0));
    }
    ValueIdIterator end() const {
        return ValueIdIterator(endValue);
    }

    ValueIdRange(ValueKind kind, uint32_t endId)
        : endValue(kind, endId) { }

    Value endValue;
};

struct InstructionBlock {
    explicit InstructionBlock(Instruction* header)
        : m_header(header) { VERIFY(categoryOf(header->opcode()) == InstructionCategory::Header); }

    std::span<Instruction> instructions() const {
        return { m_header + 1, m_header->u.blockSize };
    }
    Instruction* header() const { return m_header; }
    Opcode headerCode() const { return m_header->opcode(); }

    Instruction* begin() const { return m_header + 1; }
    Instruction* end() const { return m_header + 1 + m_header->u.blockSize; }

private:
    Instruction* m_header = nullptr;
};

struct InstructionBlockIterator {
    using value_type = InstructionBlock;
    using difference_type = int_t;

    InstructionBlockIterator() = default;
    explicit InstructionBlockIterator(Instruction* header)
        : header(header) { }

    InstructionBlock block() const { return InstructionBlock { header }; }
    InstructionBlockIterator& operator++() {
        advance();
        return *this;
    }
    InstructionBlockIterator operator++(int) {
        InstructionBlockIterator copy = *this;
        advance();
        return copy;
    }
    InstructionBlock operator*() const { return block(); }

    auto operator<=>(const InstructionBlockIterator& other) const {
        return header <=> other.header;
    }
    bool operator==(const InstructionBlockIterator&) const = default;

private:
    void advance() { header = block().end(); }

    Instruction* header = nullptr;
};
static_assert(std::forward_iterator<InstructionBlockIterator>);

struct InstructionBlockRange {
    InstructionBlockIterator begin() const {
        return InstructionBlockIterator { instructions.data() };
    }
    InstructionBlockIterator end() const {
        return InstructionBlockIterator { instructions.data() + instructions.size() };
    }

    std::span<Instruction> instructions;
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

    Value addExpression(Expression);

    Value addParameterize(Context& context, Parameterize parameterize);
    Value addRemoteExpression(Context& context, RemoteExpression expr);
    Value addMemberPointer(Context& context, MemberPointer pointer);

    Expression getExpression(ExternValue value) {
        auto instructions = getInstructions(value.id(), Opcode::ExpressionHeader);
        return Expression(&instructions.back(), instructions.size());
    }
    Parameterize getParameterize(ExternValue value) {
        VERIFY(value.kind() == ValueKind::Parameterize);
        return Parameterize::fromData(parameterizes.at(value.id()));
    }
    RemoteExpression getRemoteExpression(ExternValue value) {
        VERIFY(value.kind() == ValueKind::RemoteExpression);
        return remoteExpressions.at(value.id());
    }
    MemberPointer getMemberPointer(ExternValue value) {
        VERIFY(value.kind() == ValueKind::MemberPointer);
        return memberPointers.at(value.id());
    }
    std::strong_ordering compareParameterizes(Value a, Value b) {
        VERIFY(a.kind() == ValueKind::Parameterize);
        VERIFY(b.kind() == ValueKind::Parameterize);
        return parameterizes.label(a.id()) <=> parameterizes.label(b.id());
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
    InstructionBlockRange instructionBlocks() {
        return InstructionBlockRange { instructions };
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

    ProgramHandle baseProgram(ExternValue value) {
        if (value.kind() == ValueKind::Program)
            return value.program();
        if (value.kind() == ValueKind::Parameterize)
            return getParameterize(value).base;
        VERIFY_NOT_REACHED();
    }

    ValueIdRange parameterizeValues() const {
        return ValueIdRange(ValueKind::Parameterize, parameterizes.size());
    }
    ValueIdRange memberPointerValues() const {
        return ValueIdRange(ValueKind::MemberPointer, memberPointers.size());
    }
    ValueIdRange remoteExpressionValues() const {
        return ValueIdRange(ValueKind::RemoteExpression, remoteExpressions.size());
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
    std::vector<Instruction> instructions;
    ParameterizeSet parameterizes;
    RemoteExpressionSet remoteExpressions;
    MemberPointerSet memberPointers;

protected:
    static constexpr uint32_t INVALID_SUBCLASS_DATA = -1;

    std::optional<ExternValue> m_type;
    uint32_t m_subClassData = INVALID_SUBCLASS_DATA;
    ScopeValue m_parent;
    parse::TokenHandle parseLocation;

    const ProgramHandle* programTranslationBuffer = nullptr;
    const NamespaceHandle* namespaceTranslationBuffer = nullptr;

    int_t importInstructions(Opcode headerCode, std::span<const Instruction> instructions);

    std::span<Instruction> getInstructions(int_t offset, Opcode headerCode) {
        VERIFY(categoryOf(headerCode) == InstructionCategory::Header);
        VERIFY(instructions[offset].opcode() == headerCode);
        return { instructions.data() + offset + 1, instructions[offset].u.blockSize };
    }

    friend struct Dumper;
    friend Context; // set translation buffers
};
static_assert(sizeof(Program) == 192);

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

struct ObjectProgram : Program {
    ObjectProgram(Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location)
        : Program(ProgramKind::Object, name, parseLocation, rawParent, location) { }

    ExternValue objectType() const { return m_type.value(); }
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
    parse::TokenHandle parseLocation() const {
        VERIFY(kind() == RuntimeParameterKind::UncheckedMember);
        return u.parseLocation;
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

    void setBody(std::span<const Instruction> body) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = importInstructions(Opcode::FunctionHeader, body);
    }
    std::span<Instruction> body() {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
        return getInstructions(m_subClassData, Opcode::FunctionHeader);
    }
};

struct TypeProgram : CallableProgram, Scope {
    TypeProgram(Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location)
        : CallableProgram(ProgramKind::Type, name, parseLocation, rawParent, location) { }
};

template<typename T>
constexpr std::optional<T*> try_cast(Program* prog) {
    switch (prog->kind()) {
    case ProgramKind::Value:
        if constexpr (std::derived_from<ValueProgram, T>)
            return static_cast<T*>(prog);
        else
            return std::nullopt;
    case ProgramKind::Object:
        if constexpr (std::derived_from<ObjectProgram, T>)
            return static_cast<T*>(prog);
        else
            return std::nullopt;
    case ProgramKind::Type:
        if constexpr (std::derived_from<TypeProgram, T>)
            return static_cast<T*>(prog);
        else
            return std::nullopt;
    case ProgramKind::Function:
        if constexpr (std::derived_from<FunctionProgram, T>)
            return static_cast<T*>(prog);
        else
            return std::nullopt;
    default:
        VERIFY_NOT_REACHED();
    }
}

template<typename T>
constexpr T* cast(Program* prog) { return try_cast<T>(prog).value(); }

union ProgramUnion {
    ValueProgram value;
    ObjectProgram object;
    FunctionProgram function;
    TypeProgram type;

    ProgramUnion(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeValue rawParent, SourceLocation location) {
        switch (kind) {
        case ProgramKind::Value:
            std::construct_at(&value, name, parseLocation, rawParent, location);
            break;
        case ProgramKind::Object:
            std::construct_at(&object, name, parseLocation, rawParent, location);
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
        case ProgramKind::Object:
            std::destroy_at(&object);
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
static_assert(sizeof(ProgramUnion) == 232);

}