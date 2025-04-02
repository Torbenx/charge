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

struct ImplicitCopy {
    Expression copyFrom;
    Type type;
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

enum class ProgramKind : uint8_t {
    Global,
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
        , parseLocationOrSelfConstant(parseLocation.id()) { }

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

    Expression addMemberExpression(MemberExpression);
    MemberExpression getMemberReference(Expression e) {
        VERIFY(e.kind() == ExpressionKind::MemberExpression);
        return memberExpressions[e.id()];
    }

    Expression addCall(Call);
    Call getCall(Expression e) {
        VERIFY(e.kind() == ExpressionKind::Call);
        return Call::fromData(calls.at(e.id()));
    }

    Expression addImplicitCopy(ImplicitCopy);
    ImplicitCopy getImplicitCopy(Expression e) {
        VERIFY(e.kind() == ExpressionKind::ImplicitCopy);
        return implicitCopies[e.id()];
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
        return parse::TokenHandle { parseLocationOrSelfConstant };
    }

    void completeSignatureCheck(bool isImpl, Constant selfConstant) {
        VERIFY(status() == ProgramStatus::SignatureCheckInProgress);
        auto tag = m_fields.tag();
        tag.setStatus(ProgramStatus::SignatureChecked);
        tag.setImpl(isImpl);
        m_fields.setTag(tag);
        parseLocationOrSelfConstant = selfConstant.toUint();
    }

    ExternConstant selfConstant() const {
        VERIFY(status() == ProgramStatus::SignatureChecked);
        return Constant::fromUint(parseLocationOrSelfConstant);
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

    std::vector<Parameter> parameters;
    std::vector<Instruction> instructions;
    ParameterizeSet parameterizes;
    RemoteComputationSet remoteComputations;
    MemberPointerSet memberPointers;
    std::vector<ComputedConstantData> computations;
    std::vector<MemberExpression> memberExpressions;
    std::vector<CallData> calls;
    std::vector<ImplicitCopy> implicitCopies;

protected:
    static constexpr uint32_t INVALID_SUBCLASS_DATA = -1;

    std::optional<ExternConstant> m_type;
    uint32_t m_subClassData = INVALID_SUBCLASS_DATA;
    ScopeConstant m_parent;
    uint32_t parseLocationOrSelfConstant;

    const ProgramHandle* programTranslationBuffer = nullptr;
    const NamespaceHandle* namespaceTranslationBuffer = nullptr;

    friend struct Dumper;
    friend Context; // set translation buffers
};
static_assert(sizeof(Program) == 288);

enum class GlobalKind : uint8_t {
    Var,
    ConstVar,
    Let,
    OpenLet,
};

struct GlobalProgram : Program {
    GlobalProgram(Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location)
        : Program(ProgramKind::Global, name, parseLocation, rawParent, location) { }

    void setInitializer(Constant value) {
        VERIFY(m_subClassData == INVALID_SUBCLASS_DATA);
        m_subClassData = value.toUint();
    }

    ExternConstant initializer() const {
        VERIFY(m_subClassData != INVALID_SUBCLASS_DATA);
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
    Type type;
    Constant expectedInitializerCategory;
};

template<typename P>
concept CallableProgram = requires(P* program, int_t index) {
    { callParameters<P>::get(program) } -> std::ranges::random_access_range;
    { callParameters<P>::get(program)[index] } -> std::same_as<CallParameter>;
};

struct FunctionParameter {
private:
    SourceLocation m_location;
    Word m_name;
    Type m_type;
    Constant m_category;

public:
    FunctionParameter(SourceLocation location, Word name, Type type, Constant category)
        : m_location(location), m_name(name), m_type(type), m_category(category) { }

    SourceLocation location() const { return m_location; }
    Word name() const { return m_name; }
    Type type() const { return m_type; }
    Constant category() const { return m_category; }

    operator CallParameter() const { return { name(), type(), category() }; }
};
struct FunctionProgram : Program {
    FunctionProgram(Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location)
        : Program(ProgramKind::Function, name, parseLocation, rawParent, location) { }

    void setBody(std::vector<Instruction> body) { m_body = std::move(body); }
    std::span<const Instruction> body() { return m_body; }
    ExternConstant returnType() const { return m_type.value(); }

    std::vector<FunctionParameter> functionParameters;
    std::vector<Instruction> m_body;
};
template<>
struct callParameters<FunctionProgram> {
    static auto get(FunctionProgram* prog) {
        return std::views::transform(prog->functionParameters, [](const FunctionParameter& p) -> CallParameter { return p; });
    }
};
static_assert(CallableProgram<FunctionProgram>);

struct Member {
private:
    struct Fields {
        bool hasBit : 1;
        bool checkedBit : 1;
    };
    TaggedSourceLocation<Fields> m_fields;
    Word m_name;
    union {
        parse::TokenHandle parseLocation;
        Type type;
    } u;

public:
    Member(SourceLocation location, bool isHas, Word name, parse::TokenHandle parseLocation)
        : m_fields(Fields { .hasBit = isHas, .checkedBit = false }, location), m_name(name), u { .parseLocation = parseLocation } { }

    void setType(Type type) {
        VERIFY(!isChecked());
        auto tag = m_fields.tag();
        tag.checkedBit = true;
        m_fields.setTag(tag);
        u.type = type;
    }

    bool isHas() const { return m_fields.tag().hasBit; }
    bool isChecked() const { return m_fields.tag().checkedBit; }
    SourceLocation location() const { return m_fields.location(); }
    Type type() const {
        VERIFY(isChecked());
        return u.type;
    }
    parse::TokenHandle parseLocation() const {
        VERIFY(!isChecked());
        return u.parseLocation;
    }
    Word name() const { return m_name; }
};
struct TypeProgram : Program, Scope {
    TypeProgram(Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location)
        : Program(ProgramKind::Type, name, parseLocation, rawParent, location) { }
    std::vector<Member> members;
};
template<>
struct callParameters<TypeProgram> {
    static auto get(TypeProgram* program) {
        return std::views::transform(program->members, [](Member m) -> CallParameter { return { m.name(), m.type(), Constant(ExpressionCategory::Value) }; });
    }
};
static_assert(CallableProgram<TypeProgram>);

template<typename T>
constexpr std::optional<T*> try_cast(Program* prog) {
    switch (prog->kind()) {
    case ProgramKind::Global:
        if constexpr (std::derived_from<GlobalProgram, T>)
            return static_cast<GlobalProgram*>(prog);
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

template<typename P, template<typename> typename r>
concept implements = requires(P* program) { r<P>::get(program); };

template<template<typename> typename r>
constexpr bool try_visit(Program* prog, auto&& callable) {
    switch (prog->kind()) {
    case ProgramKind::Global:
        if constexpr (implements<GlobalProgram, r>) {
            callable(r<GlobalProgram>::get(static_cast<GlobalProgram*>(prog)));
            return true;
        } else
            return false;
    case ProgramKind::Type:
        if constexpr (implements<TypeProgram, r>) {
            callable(r<TypeProgram>::get(static_cast<TypeProgram*>(prog)));
            return true;
        } else
            return false;
    case ProgramKind::Function:
        if constexpr (implements<FunctionProgram, r>) {
            callable(r<FunctionProgram>::get(static_cast<FunctionProgram*>(prog)));
            return true;
        } else
            return false;
    default:
        VERIFY_NOT_REACHED();
    }
};

template<template<typename> typename r>
constexpr auto visit(Program* prog, auto&& callable) {
    switch (prog->kind()) {
    case ProgramKind::Global:
        if constexpr (implements<GlobalProgram, r>) {
            return callable(r<GlobalProgram>::get(static_cast<GlobalProgram*>(prog)));
        } else
            VERIFY_NOT_REACHED();
    case ProgramKind::Type:
        if constexpr (implements<TypeProgram, r>) {
            return callable(r<TypeProgram>::get(static_cast<TypeProgram*>(prog)));
        } else
            VERIFY_NOT_REACHED();
    case ProgramKind::Function:
        if constexpr (implements<FunctionProgram, r>) {
            return callable(r<FunctionProgram>::get(static_cast<FunctionProgram*>(prog)));
        } else
            VERIFY_NOT_REACHED();
    default:
        VERIFY_NOT_REACHED();
    }
};

union ProgramUnion {
    GlobalProgram global;
    FunctionProgram function;
    TypeProgram type;

    ProgramUnion(ProgramKind kind, Word name, parse::TokenHandle parseLocation, ScopeConstant rawParent, SourceLocation location) {
        switch (kind) {
        case ProgramKind::Global:
            std::construct_at(&global, name, parseLocation, rawParent, location);
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

    Program& get() { return global; }

    ~ProgramUnion() {
        switch (global.kind()) {
        case ProgramKind::Global:
            std::destroy_at(&global);
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
static_assert(sizeof(ProgramUnion) == 336);

inline constexpr Expression Expression::returnValueReference(FunctionProgram* prog) {
    return parameterReference(prog->functionParameters.size());
}

}