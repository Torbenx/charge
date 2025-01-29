#pragma once

#include <types.h>

#include <utility>

namespace sema {

enum class BuiltinId : uint32_t {

#define BUILTIN(name, cppName) cppName,
#include <sema/builtins.inc>

    COUNT,
};

enum class ExpressionCategory : uint8_t {
    Value,
    UniqueReference,
    ConstUniqueReference,
    SharedReference,
    ConstSharedReference,
};

struct Constant;

struct ProgramHandle {
    uint32_t m_id = -1;
    constexpr uint32_t id() const { return m_id; }

    bool operator==(const ProgramHandle&) const = default;
};

struct NamespaceHandle {
    uint32_t m_id = -1;
    constexpr uint32_t id() const { return m_id; }

    bool operator==(const NamespaceHandle&) const = default;
};

inline constexpr int_t CONSTANT_KIND_BITS = 8;
inline constexpr uint32_t MAX_CONSTANT_ID = (1u << (32 - CONSTANT_KIND_BITS)) - 1u;
inline constexpr uint32_t INVALID_CONSTANT_KIND_INDEX = (1u << CONSTANT_KIND_BITS) - 1u;

enum class ConstantKind : uint8_t {
    // The ordering in this enum determines the ordering of constants
    // This ordering needs to be defined between aritatry constants to make some data strutures work,
    // but it is most important between constants of the same type where it used to orient equalities.

    Program, // either not dependent or a template
    Namespace,
    TemplateSignature$Program,
    TemplateSignature$Parameterize,
    FunctionSignature$Program,
    FunctionSignature$Parameterize,
    BooleanLiteral,
    MemberPointer,

    Parameterize, // either all argument substituted or just the inherited ones
    Computed,
    RemoteComputed,

    CopyOfParameter,

    Invalid = INVALID_CONSTANT_KIND_INDEX,
};
struct Constant {
    constexpr Constant(ConstantKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) { }
    constexpr explicit Constant(ProgramHandle prog)
        : Constant(ConstantKind::Program, prog.id()) { }
    constexpr explicit Constant(NamespaceHandle ns)
        : Constant(ConstantKind::Namespace, ns.id()) { }
    constexpr Constant(BuiltinId id)
        : Constant(ProgramHandle(std::to_underlying(id))) { }

    static Constant fromUint(uint32_t x) { return std::bit_cast<Constant>(x); }
    uint32_t toUint() const { return std::bit_cast<uint32_t>(*this); }

    constexpr uint32_t id() const { return idBits; }
    constexpr ConstantKind kind() const { return (ConstantKind)kindBits; }

    constexpr ProgramHandle program() const {
        VERIFY(kind() == ConstantKind::Program);
        return { id() };
    }
    constexpr NamespaceHandle nsHandle() const {
        VERIFY(kind() == ConstantKind::Namespace);
        return { id() };
    }
    constexpr ProgramHandle templateSignatureProgram() const {
        VERIFY(kind() == ConstantKind::TemplateSignature$Program);
        return { id() };
    }
    constexpr Constant templateSignatureBaseConstant() const {
        if (kind() == ConstantKind::TemplateSignature$Program)
            return Constant(ConstantKind::Program, id());
        if (kind() == ConstantKind::TemplateSignature$Parameterize)
            return Constant(ConstantKind::Parameterize, id());
        VERIFY_NOT_REACHED();
    }
    constexpr ProgramHandle functionSignatureProgram() const {
        VERIFY(kind() == ConstantKind::FunctionSignature$Program);
        return { id() };
    }
    constexpr Constant functionSignatureBaseConstant() const {
        if (kind() == ConstantKind::FunctionSignature$Program)
            return Constant(ConstantKind::Program, id());
        if (kind() == ConstantKind::FunctionSignature$Parameterize)
            return Constant(ConstantKind::Parameterize, id());
        VERIFY_NOT_REACHED();
    }
    constexpr int_t parameterIndex() const {
        VERIFY(kind() == ConstantKind::CopyOfParameter);
        return id();
    }
    constexpr bool booleanValue() const {
        VERIFY(kind() == ConstantKind::BooleanLiteral);
        return idBits != 0;
    }

    constexpr bool operator==(const Constant&) const = default;

    uint32_t idBits : (32 - CONSTANT_KIND_BITS);
    uint32_t kindBits : CONSTANT_KIND_BITS;
};
inline constexpr Constant INVALID_CONSTANT = { ConstantKind::Invalid, MAX_CONSTANT_ID };

struct Type : Constant {
    static Type fromUint(uint32_t x) { return Type(Constant::fromUint(x)); }
    using Constant::Constant;
    constexpr explicit Type(Constant value)
        : Constant(value) { }
};

struct ExternConstant {
    constexpr ExternConstant(Constant value)
        : value(value) { }

    constexpr uint32_t id() const { return value.id(); }
    constexpr ConstantKind kind() const { return value.kind(); }
    constexpr ProgramHandle program() const { return value.program(); }

    constexpr explicit operator Constant() const { return value; }

    bool operator==(const ExternConstant&) const = default;

private:
    Constant value;
};

struct ScopeConstant {
    static constexpr ScopeConstant invalidValue() { return {}; }
    static ScopeConstant fromUint(uint32_t u) { return ScopeConstant(Constant::fromUint(u)); }

    constexpr explicit ScopeConstant(Constant value)
        : value(value) {
        VERIFY(value.kind() == ConstantKind::Program || value.kind() == ConstantKind::Namespace || value.kind() == ConstantKind::Invalid);
    }
    constexpr ScopeConstant(ProgramHandle prog)
        : value(prog) { }
    constexpr ScopeConstant(NamespaceHandle ns)
        : value(ns) { }

    constexpr ConstantKind kind() const { return value.kind(); }
    constexpr ProgramHandle program() const { return value.program(); }
    constexpr NamespaceHandle nsHandle() const { return value.nsHandle(); }

    uint32_t toUint() const { return value.toUint(); }

    bool operator==(const ScopeConstant&) const = default;

private:
    constexpr ScopeConstant()
        : value(INVALID_CONSTANT) { }

    Constant value;
};
inline constexpr ScopeConstant INVALID_SCOPE_CONSTANT = ScopeConstant::invalidValue();

enum class ExpressionKind : uint8_t {
    FirstNonConstantKind = 128,

    ParameterReference = FirstNonConstantKind,
    TemplateParameterReference,
    VariableReference,
    ReferenceReference,
    MemberExpression,
    Call,
    ImplicitCopy,
    Invalid = INVALID_CONSTANT_KIND_INDEX,
};

struct Expression {
    static constexpr Expression variableReference(uint32_t id) {
        return Expression(ExpressionKind::VariableReference, id);
    }
    static constexpr Expression referenceReference(uint32_t id) {
        return Expression(ExpressionKind::ReferenceReference, id);
    }
    static constexpr Expression parameterReference(uint32_t id) {
        return Expression(ExpressionKind::ParameterReference, id);
    }
    static constexpr Expression templateParameterReference(uint32_t id) {
        return Expression(ExpressionKind::TemplateParameterReference, id);
    }

    constexpr Expression(ExpressionKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) { }
    constexpr Expression(Constant constant)
        : Expression(std::bit_cast<Expression>(constant)) { }

    constexpr ExpressionKind kind() const { return (ExpressionKind)kindBits; }
    constexpr int_t id() const { return idBits; }

    constexpr bool isConstant() const { return kind() < ExpressionKind::FirstNonConstantKind; }
    constexpr Constant constant() const {
        VERIFY(isConstant());
        return std::bit_cast<Constant>(*this);
    }

    constexpr int_t varaibleIndex() const {
        VERIFY(kind() == ExpressionKind::VariableReference);
        return id();
    }
    constexpr int_t parameterIndex() const {
        VERIFY(kind() == ExpressionKind::ParameterReference);
        return id();
    }
    constexpr int_t templateParameterIndex() const {
        VERIFY(kind() == ExpressionKind::TemplateParameterReference);
        return id();
    }
    constexpr Constant copyTemplateParameter() const {
        VERIFY(kind() == ExpressionKind::TemplateParameterReference);
        return Constant(ConstantKind::CopyOfParameter, id());
    }
    constexpr int_t referenceIndex() const {
        VERIFY(kind() == ExpressionKind::ReferenceReference);
        return id();
    }

    constexpr bool isInstructionResult() const {
        return kind() == ExpressionKind::Call || kind() == ExpressionKind::ImplicitCopy;
    }

    constexpr bool operator==(const Expression&) const = default;

    uint32_t idBits : (32 - CONSTANT_KIND_BITS);
    uint32_t kindBits : CONSTANT_KIND_BITS;
};

inline constexpr Expression INVALID_EXPRESSION = { ExpressionKind::Invalid, MAX_CONSTANT_ID };

struct OwnedExpression : Expression {
    using Expression::Expression;

    OwnedExpression(Expression e)
        : Expression(e) { }

    OwnedExpression(const OwnedExpression&) = delete;
    OwnedExpression& operator=(const OwnedExpression&) = delete;

    OwnedExpression(OwnedExpression&& other)
        : Expression(other) { (Expression&)other = INVALID_EXPRESSION; }
    OwnedExpression& operator=(OwnedExpression&& other) {
        VERIFY(!isInstructionResult());
        (Expression&)*this = other;
        (Expression&)other = INVALID_EXPRESSION;
        return *this;
    }
    constexpr ~OwnedExpression() {
        VERIFY(!isInstructionResult());
    }

    Expression release() {
        Expression ret = *this;
        (Expression&)*this = INVALID_EXPRESSION;
        return ret;
    }
};

}
template<>
struct optional_traits<sema::ProgramHandle> {
    static constexpr sema::ProgramHandle empty_value = {};
};
template<>
struct optional_traits<sema::NamespaceHandle> {
    static constexpr sema::NamespaceHandle empty_value = {};
};
template<>
struct optional_traits<sema::Constant> {
    static constexpr sema::Constant empty_value = sema::INVALID_CONSTANT;
};
template<>
struct optional_traits<sema::Type> {
    static constexpr sema::Type empty_value = (sema::Type)sema::INVALID_CONSTANT;
};
template<>
struct optional_traits<sema::ExternConstant> {
    static constexpr sema::ExternConstant empty_value = sema::INVALID_CONSTANT;
};
template<>
struct optional_traits<sema::ScopeConstant> {
    static constexpr sema::ScopeConstant empty_value = sema::INVALID_SCOPE_CONSTANT;
};
template<>
struct optional_traits<sema::Expression> {
    static constexpr sema::Expression empty_value = sema::INVALID_EXPRESSION;
};