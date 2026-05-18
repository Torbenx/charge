#pragma once

#include <FlatTreeSet.h>
#include <parse/TokenBuffer.h>
#include <sema/Instruction.h>
#include <sema/Scope.h>

#include <ranges>

namespace sema {

struct Context;

namespace builtins {

#define BUILTIN_TYPE(name) constexpr inline Type name##_type { BuiltinId::name##_type };
#define BUILTIN(name, cppName) constexpr inline Constant cppName { BuiltinId::cppName };
#include <sema/builtins.inc>

    inline constexpr Constant false_constant { ConstantKind::BooleanLiteral, 0 };
    inline constexpr Constant true_constant { ConstantKind::BooleanLiteral, 1 };

    inline constexpr Constant self_constant { ConstantKind::Self, 0 };

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
    struct Element {
        Constant structImpl;
        uint32_t memberIndex;
    };

    bool isIdentity() const { return elements.empty(); }

    Type memberType;
    std::span<const Element> elements;
};

struct MemberPointerData {
    Type memberType;
    std::vector<MemberPointer::Element> elements;
    operator MemberPointer() const { return { memberType, elements }; }
};
struct MemberPointerSet : FlatTreeSetDetail::Base<MemberPointerSet, MemberPointerData> {
    uint32_t get(Context& context, ProgramHandle prog, MemberPointer);

private:
    friend Base;
    uint32_t makeNode(Context&, ProgramHandle, MemberPointer, TreeLabel);
    std::strong_ordering compare(Context&, ProgramHandle, MemberPointer, MemberPointer);
};

struct EnumValue {
    ExternConstant enumType;
    uint32_t valueIndex;
};
struct EnumValueSet : FlatTreeSetDetail::Base<EnumValueSet, EnumValue> {
    uint32_t get(Context& context, ProgramHandle prog, EnumValue);

private:
    friend Base;
    uint32_t makeNode(Context&, ProgramHandle, EnumValue, TreeLabel);
    std::strong_ordering compare(Context&, ProgramHandle, EnumValue, EnumValue);
};

struct CallData {
    Constant resultCategory;
    Constant callTarget;
    Type returnType;
    std::vector<Expression> arguments;
};
struct Call {
    Constant resultCategory;
    Constant callTarget;
    Type returnType;
    std::span<const Expression> arguments;

    static Call fromData(const CallData& data) {
        return { data.resultCategory, data.callTarget, data.returnType, data.arguments };
    }
};

struct ComputedConstantData {
    Expression value;
    Type type;
    std::vector<Instruction> body;
};

struct ComputedConstant {
    Expression value;
    Type type;
    std::span<const Instruction> body;
};

struct MemberExpression {
    Expression base;
    Constant memberPointer;
};

enum class ProgramStatus : uint8_t {
    Unchecked,
    SignatureCheckInProgress,
    SignatureChecked,
};

#define ENUMERATE_PROGRAM_KINDS \
    PROGRAM_KIND(Global)        \
    PROGRAM_KIND(Struct)        \
    PROGRAM_KIND(Function)      \
    PROGRAM_KIND(Enum)

enum class ProgramKind : uint8_t {
#define PROGRAM_KIND(kind) kind,
    ENUMERATE_PROGRAM_KINDS
#undef PROGRAM_KIND
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
        SourceLocation location;
        Word name;
        ExternConstant type;
        std::optional<Constant> defaultValue;

        bool implicit() const { return name.empty(); }
    };

    Program(ProgramKind kind, Word name, parse::TokenHandle parseLocation, DeclarationValue parent, SourceLocation location)
        : m_fields(Fields(kind), location)
        , m_name(name)
        , tokenRangeBegin(parseLocation)
        , m_parent(parent) { }

    Constant addComputedConstant(Context&, ComputedConstant);
    Constant addParameterize(Context& context, Parameterize parameterize);
    Constant addRemoteComputedConstant(Context& context, RemoteComputation);
    Constant addMemberPointer(Context& context, MemberPointer pointer);
    Constant addEnumValue(Context& context, EnumValue value);

    ComputedConstant getComputedConstant(ExternConstant value) const {
        VERIFY(value.kind() == ConstantKind::Computed);
        const auto& c = computations[value.id()];
        return { c.value, c.type, c.body };
    }
    Parameterize getParameterize(ExternConstant value) const {
        VERIFY(value.kind() == ConstantKind::Parameterize);
        return Parameterize::fromData(parameterizes.at(value.id()));
    }
    RemoteComputation getRemoteComputedConstant(ExternConstant value) const {
        VERIFY(value.kind() == ConstantKind::RemoteComputed);
        return remoteComputations.at(value.id());
    }
    MemberPointer getMemberPointer(ExternConstant value) const {
        VERIFY(value.kind() == ConstantKind::MemberPointer);
        return memberPointers.at(value.id());
    }
    EnumValue getEnumValue(ExternConstant value) const {
        switch (value.kind()) {
#define BUILTIN_ENUM(name, constant_kind) \
    case ConstantKind::constant_kind:     \
        return { builtins::name##_type, value.id() };
#include <sema/builtins.inc>

        case ConstantKind::EnumValue:
            return enumValues.at(value.id());
        default:
            VERIFY_NOT_REACHED();
        }
    }
    std::strong_ordering compareParameterizes(Constant a, Constant b) const {
        VERIFY(a.kind() == ConstantKind::Parameterize);
        VERIFY(b.kind() == ConstantKind::Parameterize);
        return parameterizes.label(a.id()) <=> parameterizes.label(b.id());
    }

    Expression addMemberExpression(MemberExpression);
    MemberExpression getMemberExpression(Expression e) const {
        VERIFY(e.kind() == ExpressionKind::MemberExpression);
        return memberExpressions[e.id()];
    }

    Expression addCall(CallData);
    Call getCall(Expression e) {
        VERIFY(e.kind() == ExpressionKind::Call);
        return Call::fromData(calls.at(e.id()));
    }

    SourceLocation declarationLocation() const { return m_fields.location(); }

    void setType(Type type) {
        VERIFY(!m_type.has_value());
        m_type = type;
    }

    DeclarationValue parent() const {
        return m_parent;
    }

    Word name() const { return m_name; }
    ProgramStatus status() const { return m_fields.tag().status(); }
    ProgramKind kind() const { return m_fields.tag().kind(); }
    parse::TokenRange tokenRange() const { return { tokenRangeBegin, tokenRangeEnd }; }
    bool isDependent() const {
        return !parameters.empty();
    }
    bool isTemplate() const {
        return parameters.size() > inheritedParameterCount;
    }
    int_t nonInheritedParameterCount() const { return parameters.size() - inheritedParameterCount; }
    bool isImpl() const {
        return m_fields.tag().isImpl();
    }

    void dump(Context&);

    parse::TokenHandle beginSignatureCheck() {
        VERIFY(status() == ProgramStatus::Unchecked);
        auto tag = m_fields.tag();
        tag.setStatus(ProgramStatus::SignatureCheckInProgress);
        m_fields.setTag(tag);
        return tokenRangeBegin;
    }

    void markSignatureCheckComplete(bool isImpl, Constant selfConstant) {
        VERIFY(status() == ProgramStatus::SignatureCheckInProgress);
        auto tag = m_fields.tag();
        tag.setStatus(ProgramStatus::SignatureChecked);
        tag.setImpl(isImpl);
        m_fields.setTag(tag);
        m_selfConstant = selfConstant;
    }

    ExternConstant selfConstant() const {
        VERIFY(status() == ProgramStatus::SignatureChecked);
        return m_selfConstant.value();
    }

    std::optional<ProgramHandle> baseProgram(ExternConstant value) const {
        if (value.kind() == ConstantKind::Program)
            return value.program();
        if (value.kind() == ConstantKind::Parameterize)
            return getParameterize(value).base;
        return std::nullopt;
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
    ConstantIdRange enumValueConstants() const {
        return ConstantIdRange(ConstantKind::EnumValue, enumValues.size());
    }

public:
    struct Fields {
        uint8_t kindBits : 2;
        uint8_t statusBits : 3;
        uint8_t implBit : 1 = 0;

        Fields(ProgramKind kind)
            : kindBits(std::to_underlying(kind))
            , statusBits(std::to_underlying(ProgramStatus::Unchecked)) { }

        void setStatus(ProgramStatus status) { statusBits = std::to_underlying(status); }
        ProgramStatus status() const { return (ProgramStatus)statusBits; }
        ProgramKind kind() const { return (ProgramKind)kindBits; }
        void setImpl(bool isImpl) { implBit = isImpl; }
        bool isImpl() const { return implBit != 0; }
    };
    TaggedSourceLocation<Fields> m_fields;
    uint32_t inheritedParameterCount = 0;
    Word m_name;
    parse::TokenHandle tokenRangeBegin;
    parse::TokenHandle tokenRangeEnd;

    std::vector<Parameter> parameters;
    std::vector<Instruction> instructions;
    ParameterizeSet parameterizes;
    RemoteComputationSet remoteComputations;
    MemberPointerSet memberPointers;
    EnumValueSet enumValues;
    std::vector<ComputedConstantData> computations;
    std::vector<MemberExpression> memberExpressions;
    std::vector<CallData> calls;

protected:
    static constexpr uint32_t INVALID_SUBCLASS_DATA = -1;

    std::optional<ExternConstant> m_type;
    uint32_t m_subClassData = INVALID_SUBCLASS_DATA;
    DeclarationValue m_parent;
    std::optional<ExternConstant> m_selfConstant;

    friend struct Dumper;
};
static_assert(sizeof(Program) == 288);

enum class GlobalKind : uint8_t {
    Var,
    ConstVar,
    Let,
    OpenLet,
};

struct GlobalProgram : Program {
    GlobalProgram(Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location)
        : Program(ProgramKind::Global, name, parseLocation, rawParent, location) { }

    bool hasInitializer() const {
        return m_subClassData != INVALID_SUBCLASS_DATA;
    }

    void setInitializer(Constant value) {
        VERIFY(!hasInitializer());
        m_subClassData = value.toUint();
    }
    ExternConstant initializer() const {
        VERIFY(hasInitializer());
        return Constant::fromUint(m_subClassData);
    }
    ExternConstant type() const { return m_type.value(); }

    GlobalKind globalKind() const { return m_globalKind; }

    GlobalKind m_globalKind = GlobalKind::Let;
};

template<typename P>
struct callParameters;

struct CallParameter {
    Word name;
    ExternConstant type;
    ExternConstant expectedInitializerCategory;
};

template<typename P>
concept CallableProgram = requires(P* program, int_t index) {
    { callParameters<P>::get(program) } -> std::ranges::random_access_range;
    { callParameters<P>::get(program)[index] } -> std::same_as<CallParameter>;
};

struct VariableCategory {
private:
    Constant m_data;

public:
    VariableCategory(VariableKind kind)
        : m_data(ConstantKind::Invalid, std::to_underlying(kind)) { VERIFY(kind != VariableKind::Generic); }
    explicit VariableCategory(Constant genericCategory)
        : m_data(genericCategory) { }

    bool isGeneric() const { return m_data.kind() != ConstantKind::Invalid; }
    VariableKind kind() const {
        return isGeneric() ? VariableKind::Generic : VariableKind(m_data.id());
    }

    Constant genericCategory() const {
        VERIFY(isGeneric());
        return m_data;
    }

    Constant initializerCategory() const {
        if (isGeneric())
            return m_data;
        switch (VariableKind(m_data.id())) {
        case VariableKind::Let:
        case VariableKind::Var:
            return Constant(ExpressionCategory::Value);
        case VariableKind::UniqueReference:
            return Constant(ExpressionCategory::UniqueReference);
        case VariableKind::SharedReference:
            return Constant(ExpressionCategory::SharedReference);
        case VariableKind::ConstUniqueReference:
            return Constant(ExpressionCategory::ConstUniqueReference);
        case VariableKind::ConstSharedReference:
            return Constant(ExpressionCategory::ConstSharedReference);
        default:
            VERIFY_NOT_REACHED();
        }
    }
};

struct FunctionProgram : Program {
    struct Parameter {
    private:
        SourceLocation m_location;
        Word m_name;
        Type m_type;
        VariableCategory m_category;

    public:
        Parameter(SourceLocation location, Word name, Type type, VariableCategory category)
            : m_location(location), m_name(name), m_type(type), m_category(category) { }

        SourceLocation location() const { return m_location; }
        Word name() const { return m_name; }
        Type type() const { return m_type; }
        VariableKind kind() const { return m_category.kind(); }
        VariableCategory category() const { return m_category; }

        operator CallParameter() const { return { name(), type(), category().initializerCategory() }; }
    };
    FunctionProgram(Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location)
        : Program(ProgramKind::Function, name, parseLocation, rawParent, location) { }

    void setBody(std::vector<Instruction> body) { m_body = std::move(body); }
    std::span<const Instruction> body() { return m_body; }
    ExternConstant returnType() const { return m_type.value(); }

    std::vector<Parameter> functionParameters;
    std::vector<Instruction> m_body;
};
template<>
struct callParameters<FunctionProgram> {
    static auto get(FunctionProgram* prog) {
        return std::views::transform(prog->functionParameters, [](const FunctionProgram::Parameter& p) -> CallParameter { return p; });
    }
};
static_assert(CallableProgram<FunctionProgram>);

struct ScopeProgram : Program, Scope {
    ScopeProgram(ProgramKind kind, Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location)
        : Program(kind, name, parseLocation, rawParent, location) { }
};

struct StructProgram : ScopeProgram {
    struct Member {
    private:
        struct Fields {
            bool baseBit : 1;
            bool checkedBit : 1;
        };
        TaggedSourceLocation<Fields> m_fields;
        Word m_name;
        union {
            parse::TokenHandle parseLocation;
            ExternConstant type;
        } u;

    public:
        Member(SourceLocation location, bool isBase, Word name, parse::TokenHandle parseLocation)
            : m_fields(Fields { .baseBit = isBase, .checkedBit = false }, location), m_name(name), u { .parseLocation = parseLocation } { }

        void setType(Type type) {
            VERIFY(!isChecked());
            auto tag = m_fields.tag();
            tag.checkedBit = true;
            m_fields.setTag(tag);
            u.type = type;
        }

        bool isBase() const { return m_fields.tag().baseBit; }
        bool isChecked() const { return m_fields.tag().checkedBit; }
        SourceLocation location() const { return m_fields.location(); }
        ExternConstant type() const {
            VERIFY(isChecked());
            return u.type;
        }
        parse::TokenHandle parseLocation() const {
            VERIFY(!isChecked());
            return u.parseLocation;
        }
        Word name() const { return m_name; }
    };

    StructProgram(Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location)
        : ScopeProgram(ProgramKind::Struct, name, parseLocation, rawParent, location) { }
    std::vector<Member> members;
};
template<>
struct callParameters<StructProgram> {
    static auto get(StructProgram* program) {
        return std::views::transform(program->members, [](StructProgram::Member m) -> CallParameter { return { m.name(), m.type(), Constant(ExpressionCategory::Value) }; });
    }
};
static_assert(CallableProgram<StructProgram>);

struct EnumProgram : ScopeProgram {
    struct Value {
    private:
        struct Fields {
            bool checkedBit : 1;
        };
        TaggedSourceLocation<Fields> m_fields;
        Word m_name;
        union {
            parse::TokenHandle parseLocation;
            std::optional<ExternConstant> explicitValue;
        } u;

    public:
        Value(SourceLocation location, Word name, parse::TokenHandle parseLocation)
            : m_fields(Fields { .checkedBit = false }, location), m_name(name), u { .parseLocation = parseLocation } { }
        Value(SourceLocation location, Word name, std::optional<ExternConstant> explicitValue)
            : m_fields(Fields { .checkedBit = true }, location), m_name(name), u { .explicitValue = explicitValue } { }

        bool isChecked() const { return m_fields.tag().checkedBit; }
        SourceLocation location() const { return m_fields.location(); }
        Word name() const { return m_name; }
        std::optional<ExternConstant> explicitValue() const {
            VERIFY(isChecked());
            return u.explicitValue;
        }
        parse::TokenHandle parseLocation() const {
            VERIFY(!isChecked());
            return u.parseLocation;
        }

        void setValue(std::optional<ExternConstant> explicitValue) {
            VERIFY(!isChecked());
            auto tag = m_fields.tag();
            tag.checkedBit = true;
            m_fields.setTag(tag);
            u.explicitValue = explicitValue;
        }
    };

    EnumProgram(Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location)
        : ScopeProgram(ProgramKind::Enum, name, parseLocation, rawParent, location) { }
    std::vector<Value> values;
};

template<typename T>
constexpr std::optional<T*> try_cast(Program* prog) {
    switch (prog->kind()) {
#define PROGRAM_KIND(kind)                                 \
    case ProgramKind::kind:                                \
        if constexpr (std::derived_from<kind##Program, T>) \
            return static_cast<kind##Program*>(prog);      \
        else                                               \
            return std::nullopt;

        ENUMERATE_PROGRAM_KINDS
#undef PROGRAM_KIND

    default:
        VERIFY_NOT_REACHED();
    }
}

template<typename T>
constexpr T* cast(Program* prog) { return try_cast<T>(prog).value(); }

template<typename P, template<typename> typename r>
concept implements = requires(P* program) { r<P>::get(program); };

template<template<typename> typename r>
constexpr bool try_visit(Program* prog, auto&& callable) {
    switch (prog->kind()) {
#define PROGRAM_KIND(kind)                                                      \
    case ProgramKind::kind:                                                     \
        if constexpr (implements<kind##Program, r>) {                           \
            callable(r<kind##Program>::get(static_cast<kind##Program*>(prog))); \
            return true;                                                        \
        } else                                                                  \
            return false;

        ENUMERATE_PROGRAM_KINDS
#undef PROGRAM_KIND

    default:
        VERIFY_NOT_REACHED();
    }
};

template<template<typename> typename r>
constexpr auto visit(Program* prog, auto&& callable) {
    switch (prog->kind()) {
#define PROGRAM_KIND(kind)                                                             \
    case ProgramKind::kind:                                                            \
        if constexpr (implements<kind##Program, r>) {                                  \
            return callable(r<kind##Program>::get(static_cast<kind##Program*>(prog))); \
        } else                                                                         \
            VERIFY_NOT_REACHED();

        ENUMERATE_PROGRAM_KINDS
#undef PROGRAM_KIND

    default:
        VERIFY_NOT_REACHED();
    }
};

union ProgramUnion {
    GlobalProgram m_global;
    FunctionProgram m_function;
    StructProgram m_struct;
    EnumProgram m_enum;

    ProgramUnion(ProgramKind kind, Word name, parse::TokenHandle parseLocation, DeclarationValue rawParent, SourceLocation location) {
        switch (kind) {
        case ProgramKind::Global:
            std::construct_at(&m_global, name, parseLocation, rawParent, location);
            break;
        case ProgramKind::Function:
            std::construct_at(&m_function, name, parseLocation, rawParent, location);
            break;
        case ProgramKind::Struct:
            std::construct_at(&m_struct, name, parseLocation, rawParent, location);
            break;
        case ProgramKind::Enum:
            std::construct_at(&m_enum, name, parseLocation, rawParent, location);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }

    Program& get() { return m_global; }

    ~ProgramUnion() {
        switch (m_global.kind()) {
        case ProgramKind::Global:
            std::destroy_at(&m_global);
            break;
        case ProgramKind::Function:
            std::destroy_at(&m_function);
            break;
        case ProgramKind::Struct:
            std::destroy_at(&m_struct);
            break;
        case ProgramKind::Enum:
            std::destroy_at(&m_enum);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
};
static_assert(sizeof(ProgramUnion) == 336);

inline constexpr Expression Expression::returnValueReference(FunctionProgram* prog) {
    return parameterReference(prog->functionParameters.size());
}

}