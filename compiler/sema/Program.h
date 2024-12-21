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
#define BUILTIN(name, cppName) constexpr inline Constant cppName { BuiltinId::cppName };
#include <sema/builtins.inc>

inline constexpr Constant false_constant { ConstantKind::BooleanLiteral, 0 };
inline constexpr Constant true_constant { ConstantKind::BooleanLiteral, 1 };

};

enum class ExpressionCategory : uint8_t {
    Value,
    UniqueReference,
    SharedReference,
    ConstReference,
};

struct ParameterizeData {
    ProgramHandle base;
    std::vector<Constant> arguments;
};
struct Parameterize {
    ProgramHandle base;
    std::span<const Constant> arguments;

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

struct RemoteComputation {
    Constant base;
    ExternConstant computation;
};
struct RemoteComputationSet : FlatTreeSetDetail::Base<RemoteComputationSet, RemoteComputation> {
    uint32_t get(Context& context, ProgramHandle prog, RemoteComputation);

private:
    friend Base;
    uint32_t makeNode(Context&, ProgramHandle, RemoteComputation, TreeLabel);
    std::strong_ordering compare(Context&, ProgramHandle, RemoteComputation, RemoteComputation);
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

struct ComputedConstantData {
    ValueSlot value;
    Type type;
    std::vector<Instruction*> body;
};

struct ComputedConstant {
    ValueSlot value;
    Type type;
    std::span<Instruction* const> body;
};

struct MemberReference {
    Reference base;
    Constant memberPointer;
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

struct ConstantIdIterator {
    using value_type = Constant;
    using difference_type = int_t;

    ConstantIdIterator() = default;
    explicit ConstantIdIterator(Constant value)
        : m_value(value) { }
    ConstantIdIterator(const ConstantIdIterator&) = default;
    ConstantIdIterator(ConstantIdIterator&&) = default;
    ConstantIdIterator& operator=(const ConstantIdIterator&) = default;
    ConstantIdIterator& operator=(ConstantIdIterator&&) = default;

    Constant value() const {
        return m_value;
    }
    ConstantIdIterator& operator++() {
        advance();
        return *this;
    }
    ConstantIdIterator operator++(int) {
        ConstantIdIterator copy = *this;
        advance();
        return copy;
    }
    Constant operator*() const { return value(); }

    auto operator<=>(const ConstantIdIterator& other) const {
        VERIFY(m_value.kind() == other.m_value.kind());
        return m_value.id() <=> other.m_value.id();
    }
    bool operator==(const ConstantIdIterator&) const = default;

private:
    void advance() {
        m_value = Constant(m_value.kind(), m_value.id() + 1);
    }
    Constant m_value = INVALID_CONSTANT;
};
static_assert(std::forward_iterator<ConstantIdIterator>);

struct ConstantIdRange {
    ConstantIdIterator begin() const {
        return ConstantIdIterator(Constant(endValue.kind(), 0));
    }
    ConstantIdIterator end() const {
        return ConstantIdIterator(endValue);
    }

    ConstantIdRange(ConstantKind kind, uint32_t endId)
        : endValue(kind, endId) { }

    Constant endValue;
};

struct Program {
    struct Parameter {
        Word name;
        ExternConstant type;
        std::optional<Constant> defaultValue;

        bool implicit() const { return name.empty(); }
    };

    Program(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeConstant parent, SourceLocation location)
        : m_fields(Fields(kind), location)
        , m_name(name)
        , m_parent(parent)
        , parseLocation(parseLocation) { }

    Constant addComputedConstant(Context&, ComputedConstant);
    Constant addParameterize(Context& context, Parameterize parameterize);
    Constant addRemoteComputedConstant(Context& context, RemoteComputation);
    Constant addMemberPointer(Context& context, MemberPointer pointer);

    ComputedConstant getComputedConstant(ExternConstant value) {
        VERIFY(value.kind() == ConstantKind::Computed);
        const auto& c = computations[value.id()];
        return { c.value, c.type, c.body };
    }
    Parameterize getParameterize(ExternConstant value) {
        VERIFY(value.kind() == ConstantKind::Parameterize);
        return Parameterize::fromData(parameterizes.at(value.id()));
    }
    RemoteComputation getRemoteComputedConstant(ExternConstant value) {
        VERIFY(value.kind() == ConstantKind::RemoteComputed);
        return remoteComputations.at(value.id());
    }
    MemberPointer getMemberPointer(ExternConstant value) {
        VERIFY(value.kind() == ConstantKind::MemberPointer);
        return memberPointers.at(value.id());
    }
    std::strong_ordering compareParameterizes(Constant a, Constant b) {
        VERIFY(a.kind() == ConstantKind::Parameterize);
        VERIFY(b.kind() == ConstantKind::Parameterize);
        return parameterizes.label(a.id()) <=> parameterizes.label(b.id());
    }

    Reference addMemberReference(MemberReference);
    MemberReference getMemberReference(Reference e) {
        VERIFY(e.kind() == ReferenceKind::MemberExpression);
        return memberReferences[e.id()];
    }

    SourceLocation declarationLocation() const { return m_fields.location(); }

    void setType(Type type) {
        VERIFY(!m_type.has_value());
        m_type = type;
    }

    ScopeConstant parent() const {
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
    ScopeConstant translate(ScopeConstant value) const {
        if (value.kind() == ConstantKind::Program)
            return translate(value.program());
        if (value.kind() == ConstantKind::Namespace)
            return translate(value.nsHandle());
        return value;
    }

    ProgramHandle baseProgram(ExternConstant value) {
        if (value.kind() == ConstantKind::Program)
            return value.program();
        if (value.kind() == ConstantKind::Parameterize)
            return getParameterize(value).base;
        VERIFY_NOT_REACHED();
    }

    ConstantIdRange computedConstants() const {
        return ConstantIdRange(ConstantKind::Computed, computations.size());
    }
    ConstantIdRange parameterizeConstants() const {
        return ConstantIdRange(ConstantKind::Parameterize, parameterizes.size());
    }
    ConstantIdRange memberPointerConstants() const {
        return ConstantIdRange(ConstantKind::MemberPointer, memberPointers.size());
    }
    ConstantIdRange remoteComputedConstants() const {
        return ConstantIdRange(ConstantKind::RemoteComputed, remoteComputations.size());
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
    RemoteComputationSet remoteComputations;
    MemberPointerSet memberPointers;
    std::vector<ComputedConstantData> computations;
    std::vector<MemberReference> memberReferences;

protected:
    static constexpr uint32_t INVALID_SUBCLASS_DATA = -1;

    std::optional<ExternConstant> m_type;
    uint32_t m_subClassData = INVALID_SUBCLASS_DATA;
    ScopeConstant m_parent;
    parse::TokenHandle parseLocation;

    const ProgramHandle* programTranslationBuffer = nullptr;
    const NamespaceHandle* namespaceTranslationBuffer = nullptr;

    friend struct Dumper;
    friend Context; // set translation buffers
};
static_assert(sizeof(Program) == 240);

struct ValueProgram : Program {
    ValueProgram(Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location)
        : Program(ProgramKind::Value, name, parseLocation, rawParent, location) { }

    void setValue(Constant value) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = value.toUint();
    }

    ExternConstant value() const {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
        return Constant::fromUint(m_subClassData);
    }
    ExternConstant type() const { return m_type.value(); }
};

struct ObjectProgram : Program {
    ObjectProgram(Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location)
        : Program(ProgramKind::Object, name, parseLocation, rawParent, location) { }

    ExternConstant objectType() const { return m_type.value(); }
};

enum class RuntimeParameterKind : uint8_t {
    UncheckedMember,
    Member,
    HasMember,

    LetVariable,
    VarVariable,
    UniqueReference,
    SharedReference,
    ConstReference,
};

inline ExpressionCategory expectedInitializerCategory(RuntimeParameterKind kind) {
    switch (kind) {
    case RuntimeParameterKind::HasMember:
    case RuntimeParameterKind::Member:
    case RuntimeParameterKind::VarVariable:
    case RuntimeParameterKind::LetVariable:
        return ExpressionCategory::Value;
    case RuntimeParameterKind::UniqueReference:
        return ExpressionCategory::UniqueReference;
    case RuntimeParameterKind::SharedReference:
        return ExpressionCategory::SharedReference;
    case RuntimeParameterKind::ConstReference:
        return ExpressionCategory::ConstReference;
    default:
        VERIFY_NOT_REACHED();
    }
}

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
    CallableProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location)
        : Program(kind, name, parseLocation, rawParent, location) { }

    std::vector<RuntimeParameter> runtimeParameters;
};

struct FunctionProgram : CallableProgram {
    FunctionProgram(Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location)
        : CallableProgram(ProgramKind::Function, name, parseLocation, rawParent, location) { }

    void setBody(std::span<Instruction* const> body) { m_body.assign(body.begin(), body.end()); }
    std::span<Instruction* const> body() { return m_body; }
    ExternConstant returnType() const { return m_type.value(); }

    std::vector<Instruction*> m_body;
};

struct TypeProgram : CallableProgram, Scope {
    TypeProgram(Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location)
        : CallableProgram(ProgramKind::Type, name, parseLocation, rawParent, location) { }
};

template<typename T>
constexpr std::optional<T*> try_cast(Program* prog) {
    switch (prog->kind()) {
    case ProgramKind::Value:
        if constexpr (std::derived_from<ValueProgram, T>)
            return static_cast<ValueProgram*>(prog);
        else
            return std::nullopt;
    case ProgramKind::Object:
        if constexpr (std::derived_from<ObjectProgram, T>)
            return static_cast<ObjectProgram*>(prog);
        else
            return std::nullopt;
    case ProgramKind::Type:
        if constexpr (std::derived_from<TypeProgram, T>)
            return static_cast<TypeProgram*>(prog);
        else
            return std::nullopt;
    case ProgramKind::Function:
        if constexpr (std::derived_from<FunctionProgram, T>)
            return static_cast<FunctionProgram*>(prog);
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

    ProgramUnion(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location) {
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
static_assert(sizeof(ProgramUnion) == 288);

}