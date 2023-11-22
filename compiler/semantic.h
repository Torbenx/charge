#pragma once

#include "expr.h"

#define ENUMERATE_INSTRUCTIONS              \
    INST(ForeignConstant, foreign_constant) \
    INST(Load, unary)                       \
    INST(Nop, unary)                        \
    INST(Call, call)                        \
    INST(Allocate, unary)                   \
    INST(Deallocate, binary)                \
    INST(Store, binary)                     \
    INST(ParameterSlot, nullary)            \
    INST(ReturnSlot, nullary)

enum class Opcode : uint16_t {

#define INST(name, layout) name,
    ENUMERATE_INSTRUCTIONS
#undef INST

};

enum class ValuePhase : uint8_t {
    Literal = 0,
    Constant = 1,
    Runtime = 2,
};
enum class ValueCategory : uint8_t {
    Invalid,
    // p-value: A pure value that directly corrisponds to a SSAName. Must be of value type.
    PValue,
    // l-value: A value with storage. The (pure) value is obtained by loading from the storage.
    LValue,
    // r-value: Like l-value but the storage is owned by the value. Anyone that uses an r-value
    //          must be sure to deallocate the storage when done with the value.
    RValue,
};

struct InstructionOperand {
    static constexpr uint16_t MAX_ID = 0x7fff;

    uint16_t encoded;
    // constant: Is this a constant from the prespective of the stream
    //           the instruction is in?
    constexpr InstructionOperand(bool constant, uint16_t id)
        : encoded(id | (constant ? (uint16_t)0x8000 : (uint16_t)0)) { }
    constexpr InstructionOperand()
        : InstructionOperand(false, MAX_ID) { }

    bool constant() const { return encoded & (uint16_t)0x8000; }
    uint16_t id() const { return encoded & (uint16_t)0x7fff; }

    constexpr bool operator==(const InstructionOperand&) const = default;
};
template<>
struct optional_traits<InstructionOperand> {
    static constexpr InstructionOperand empty_value = {};
};
struct ConstantStreamOperand : InstructionOperand {
    ConstantStreamOperand() = default;
    explicit ConstantStreamOperand(InstructionOperand op)
        : InstructionOperand(op) { }
    ValuePhase phase() const { return constant() ? ValuePhase::Literal : ValuePhase::Constant; }
    constexpr bool operator==(const ConstantStreamOperand&) const = default;
};
struct ConstantStreamTypeOperand : ConstantStreamOperand { };
template<>
struct optional_traits<ConstantStreamOperand> {
    static constexpr ConstantStreamOperand empty_value = {};
};
template<>
struct optional_traits<ConstantStreamTypeOperand> {
    static constexpr ConstantStreamTypeOperand empty_value = {};
};
struct ConstantStreamValue {
    ValueCategory category = ValueCategory::Invalid;
    ConstantStreamOperand primary;
    ConstantStreamTypeOperand type;
    ConstantStreamValue() = default;
    ConstantStreamValue(ValueCategory category, ConstantStreamOperand primary, ConstantStreamTypeOperand type)
        : category(category), primary(primary), type(type) { }
};

struct RuntimeStreamOperand : InstructionOperand {
    RuntimeStreamOperand() = default;
    explicit RuntimeStreamOperand(InstructionOperand op)
        : InstructionOperand(op) { }
    ValuePhase phase() const { return constant() ? ValuePhase::Constant : ValuePhase::Runtime; }
    constexpr bool operator==(const RuntimeStreamOperand&) const = default;
    ConstantStreamOperand asConstant() const {
        VERIFY(phase() == ValuePhase::Constant);
        return ConstantStreamOperand(InstructionOperand(false, id()));
    }
};

struct Instruction {
    static constexpr InstructionOperand UNUSED_OPERAND = {};
    uint64_t op : 16;
    uint64_t a : 16;
    uint64_t b : 16;
    uint64_t c : 16;

    Instruction(Opcode op, InstructionOperand a, InstructionOperand b, InstructionOperand c)
        : op(std::to_underlying(op)), a(a.encoded), b(b.encoded), c(c.encoded) { }
    Instruction(Opcode op, InstructionOperand a, InstructionOperand b, uint16_t c)
        : op(std::to_underlying(op)), a(a.encoded), b(b.encoded), c(c) { }

    Opcode opcode() const { return (Opcode)op; }
    void setOp(Opcode newOp) { op = std::to_underlying(newOp); }
    void setA(InstructionOperand newA) { a = newA.encoded; }
    void setB(InstructionOperand newB) { b = newB.encoded; }
    void setC(InstructionOperand newC) { c = newC.encoded; }
};

struct SSAName {
    ValuePhase m_phase;
    uint16_t m_id;

    SSAName()
        : m_phase(ValuePhase::Literal), m_id(0) { }
    SSAName(ValuePhase phase, size_t id)
        : m_phase(phase), m_id(id) { VERIFY(id <= InstructionOperand::MAX_ID); }
    explicit SSAName(ConstantStreamOperand operand)
        : SSAName(operand.phase(), operand.id()) { }
    explicit SSAName(RuntimeStreamOperand operand)
        : SSAName(operand.phase(), operand.id()) { }

    ValuePhase phase() const { return m_phase; }
    uint16_t id() const { return m_id; }
    InstructionOperand localize(ValuePhase targetPhase) const {
        if (phase() == targetPhase)
            return { false, id() };
        if (std::to_underlying(phase()) + 1 == std::to_underlying(targetPhase))
            return { true, id() };
        VERIFY_NOT_REACHED();
    }
    ConstantStreamOperand localizeConstant() const {
        return ConstantStreamOperand { localize(ValuePhase::Constant) };
    }
    RuntimeStreamOperand localizeRuntime() const {
        return RuntimeStreamOperand { localize(ValuePhase::Runtime) };
    }
};

struct Type : SSAName {
    Type() = default;
    explicit Type(SSAName value)
        : SSAName(value) { }
    explicit Type(ConstantStreamTypeOperand operand)
        : SSAName(operand) { }
    ConstantStreamTypeOperand localizeConstant() const {
        return { SSAName::localizeConstant() };
    }
};
struct PureValue : SSAName {
    Type m_type;
    Type type() const { return m_type; }
};
struct Value {
    uint16_t primaryId;
    uint16_t typeId;
    uint16_t primaryPhase : 2;
    uint16_t typePhase : 2;
    uint16_t m_category : 2;

    ValueCategory category() const { return (ValueCategory)m_category; }
    bool valid() const { return category() != ValueCategory::Invalid; }
    Type type() const {
        VERIFY(valid());
        return (Type)SSAName { (ValuePhase)typePhase, typeId };
    }
    SSAName primary() const {
        VERIFY(valid());
        return { (ValuePhase)primaryPhase, primaryId };
    }
    bool isLiteral() const {
        return category() == ValueCategory::PValue && primary().phase() == ValuePhase::Literal;
    }
    PureValue asPureValue() const {
        VERIFY(category() == ValueCategory::PValue);
        return { primary(), type() };
    }
    Value asLValue() const {
        VERIFY(category() == ValueCategory::LValue || category() == ValueCategory::RValue);
        return { ValueCategory::LValue, primary(), type() };
    }
    ConstantStreamValue localizeConstant() const {
        return { category(), primary().localizeConstant(), type().localizeConstant() };
    }

    Value(ValueCategory category, SSAName primary, Type type)
        : primaryId(primary.id())
        , typeId(type.id())
        , primaryPhase(std::to_underlying(primary.phase()))
        , typePhase(std::to_underlying(type.phase()))
        , m_category(std::to_underlying(category)) { }
    Value(PureValue value)
        : Value(ValueCategory::PValue, value, value.type()) { }
    static Value lvalue(SSAName storage, Type type) {
        return Value(ValueCategory::LValue, storage, type);
    }
};

// This class verifies that the storage for r-values has been deallocated when it is destructed.
struct OwnedValue : Value {
    static OwnedValue rvalue(SSAName storage, Type type) {
        return { rvalue_tag(), storage, type };
    }
    OwnedValue(Value value)
        : Value(value) {
        // Don't allow taking ownership of an r-value by an implicit convertsion.
        VERIFY(category() != ValueCategory::RValue);
    }
    OwnedValue(PureValue value)
        : Value(value) { }
    OwnedValue(const OwnedValue&) = delete;
    OwnedValue(OwnedValue&& other)
        : Value(other) {
        other.toMovedFromState();
    }
    OwnedValue& operator=(const OwnedValue&) = delete;
    OwnedValue& operator=(OwnedValue&& other) {
        *(Value*)this = other;
        other.toMovedFromState();
        return *this;
    }
    Value releaseValue() {
        Value ret = *this;
        toMovedFromState();
        return ret;
    }
    ~OwnedValue() {
        VERIFY(category() != ValueCategory::RValue);
    }

private:
    void toMovedFromState() {
        *(Value*)this = Value(ValueCategory::Invalid, SSAName(), Type());
    }
    struct rvalue_tag { };
    OwnedValue(rvalue_tag, SSAName storage, Type type)
        : Value(ValueCategory::RValue, storage, type) { }
};

enum class ConstantType : uint8_t {
    Decl,
};
struct TypedConstant {
    ConstantType type;
    uint64_t encodedValue;

    TypedConstant(ConstantType type, uint64_t encodedValue)
        : type(type), encodedValue(encodedValue) { }
    TypedConstant(Decl* decl)
        : type(ConstantType::Decl), encodedValue((uintptr_t)decl) { }

    Decl* asDecl() const {
        VERIFY(type == ConstantType::Decl);
        return (Decl*)(uintptr_t)encodedValue;
    }

    bool operator==(const TypedConstant& other) const = default;
};
constexpr bool compareConstantsOfSameType(const TypedConstant& left, const TypedConstant& right) {
    VERIFY(left.type == right.type);
    return left.encodedValue == right.encodedValue;
}

struct InstructionStream {
    std::vector<uint16_t> definitions;
    std::vector<Instruction> stream;
    ValuePhase stream_phase;

    constexpr InstructionStream(ValuePhase phase)
        : stream_phase(phase) { }

    template<Opcode op, typename... Args>
    auto emit(Args... args);

    InstructionOperand localize(SSAName name) const {
        return name.localize(stream_phase);
    }

    // Must only be called directly before emitting an instruction into the stream.
    SSAName allocateName();
    SSAName emit_nullary(Opcode op);
    SSAName emit_unary(Opcode op, SSAName in);
    SSAName emit_binary(Opcode op, SSAName in1, SSAName in2);
    SSAName emit_foreign_constant(Opcode op, SSAName decl, ConstantStreamOperand constant);
    SSAName emit_call(Opcode op, SSAName argsBase, uint16_t count);
};

enum class ParameterModel : uint8_t {
    Template,
    ImplicitTemplate,
    Let,
    In,
    Var,
    InOut,
    Out,
};
struct CheckedParameter {
    Word name;
    ParameterModel model;
    ConstantStreamTypeOperand type;
    RuntimeStreamOperand slot;
};
struct DeclProgram {
    std::vector<uint64_t> encodedLiteralValues;
    std::vector<ConstantType> literalTypes;
    std::vector<uint16_t> literalConstants;
    InstructionStream constantStream { ValuePhase::Constant };
    InstructionStream runtimeStream { ValuePhase::Runtime };

    TypedConstant literal(uint16_t index) const {
        return { literalTypes[index], encodedLiteralValues[index] };
    }
    TypedConstant literal(SSAName name) const {
        VERIFY(name.phase() == ValuePhase::Literal);
        return literal(name.id());
    }
    SSAName emitLiteral(TypedConstant);

    std::vector<CheckedParameter> parameters;
    std::optional<ConstantStreamOperand> completeDeclaringDeclSlot;
    bool m_hasAnyTemplateParameters = false;
    void addParameter(Word name, ParameterModel model, ConstantStreamTypeOperand type, RuntimeStreamOperand slot) {
        parameters.push_back({ name, model, type, slot });
    }
    void addTemplateParameter(Word name, ConstantStreamTypeOperand type, RuntimeStreamOperand slot) {
        m_hasAnyTemplateParameters = true;
        parameters.push_back({ name, ParameterModel::Template, type, slot });
    }
    void addImplicitTemplateParameter(ConstantStreamTypeOperand type, RuntimeStreamOperand slot) {
        m_hasAnyTemplateParameters = true;
        parameters.push_back({ Word(), ParameterModel::ImplicitTemplate, type, slot });
    }
    bool templated() { return m_hasAnyTemplateParameters; }
    bool declaredInTemplate() { return completeDeclaringDeclSlot.has_value(); }
    bool templatedOrDeclaredInTemplate() { return templated() || declaredInTemplate(); }

    ParameterizedDecl* theParameterizedDecl() { return reinterpret_cast<ParameterizedDecl*>(this + 1); }
    StaticDecl* theDecl() { return theParameterizedDecl()->theDecl(); }
};
struct TypeDecl::Program : DeclProgram { };
struct CheckedFunctionDecl {
    ConstantStreamTypeOperand returnType;
    RuntimeStreamOperand returnSlot;
};
struct FunctionDecl::Program : CheckedFunctionDecl, DeclProgram { };
struct CheckedStaticVariableDecl {
    ConstantStreamValue value;
};
struct StaticVariableDecl::Program : CheckedStaticVariableDecl, DeclProgram { };

struct SemanticContext {
    struct ErrorHandler {
        virtual OwnedValue unresolvedIdentifier() = 0;
    };
    struct Instrumenter { };

    ErrorHandler* errorHandler = nullptr;
    Instrumenter* instrumenter = nullptr;

    WordStringTable* wordTable = nullptr;

    void check(StaticDeclContext*);
    void requireSignature(Decl*);
    void signatureCheckTemplateParameters(ParameterizedDecl&);
    void signatureCheckTypeDecl(TypeDecl&);
    void signatureCheckStaticVariableDecl(StaticVariableDecl&);
    void signatureCheckFunctionDecl(FunctionDecl&);
    void checkBody(FunctionDecl&);
};

void dumpIR(Decl*, const WordStringTable&);