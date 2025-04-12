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
struct FunctionProgram;

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
    TemplateSignature$Parameterize, // dependent
    FunctionSignature$Program,
    FunctionSignature$Parameterize, // non-dependent
    BooleanLiteral,
    ExpressionCategoryLiteral,
    MemberPointer,
    EnumValue,

    Parameterize, // either all argument substituted or just the inherited ones
    Self,
    Computed,
    RemoteComputed,

    CopyOfParameter,
    CopyOfOpenGlobal$Program,
    CopyOfOpenGlobal$Parameterize, // non-dependent

    Invalid = INVALID_CONSTANT_KIND_INDEX,
};
struct Constant {
    constexpr Constant(ConstantKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) { }
    constexpr explicit Constant(ProgramHandle prog)
        : Constant(ConstantKind::Program, prog.id()) { }
    constexpr explicit Constant(NamespaceHandle ns)
        : Constant(ConstantKind::Namespace, ns.id()) { }
    constexpr explicit Constant(ExpressionCategory category)
        : Constant(ConstantKind::ExpressionCategoryLiteral, std::to_underlying(category)) { }
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
    constexpr Constant copiedGlobal() const {
        if (kind() == ConstantKind::CopyOfOpenGlobal$Program)
            return Constant(ConstantKind::Program, id());
        if (kind() == ConstantKind::CopyOfOpenGlobal$Parameterize)
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
    constexpr bool isEnumValueLiteral() const {
        switch (kind()) {
#define BUILTIN_ENUM(name, constant_kind) \
    case ConstantKind::constant_kind:     \
        return true;
#include <sema/builtins.inc>

        case ConstantKind::EnumValue:
            return true;
        default:
            return false;
        }
    }

    constexpr ExpressionCategory expressionCategory() const {
        VERIFY(kind() == ConstantKind::ExpressionCategoryLiteral);
        return (ExpressionCategory)idBits;
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

enum class DeclarationValueKind : uint8_t {
    Program,
    Namespace,
    Member,
    EnumValue,

    Invalid = INVALID_CONSTANT_KIND_INDEX
};

struct DeclarationValue {
    static DeclarationValue fromUint(uint32_t u) { return std::bit_cast<DeclarationValue>(u); }

    static DeclarationValue fromConstant(Constant c) {
        switch (c.kind()) {
        case ConstantKind::Program:
            return c.program();
        case ConstantKind::Namespace:
            return c.nsHandle();
        default:
            VERIFY_NOT_REACHED();
        }
    }

    constexpr DeclarationValue(DeclarationValueKind kind, uint32_t id)
        : idBits(id), kindBits(std::to_underlying(kind)) {
    }
    constexpr DeclarationValue(ProgramHandle prog)
        : DeclarationValue(DeclarationValueKind::Program, prog.id()) { }
    constexpr DeclarationValue(NamespaceHandle ns)
        : DeclarationValue(DeclarationValueKind::Namespace, ns.id()) { }

    constexpr DeclarationValueKind kind() const { return (DeclarationValueKind)kindBits; }
    uint32_t id() const { return idBits; }

    constexpr ProgramHandle program() const {
        VERIFY(kind() == DeclarationValueKind::Program);
        return ProgramHandle(id());
    }
    constexpr NamespaceHandle nsHandle() const {
        VERIFY(kind() == DeclarationValueKind::Namespace);
        return NamespaceHandle(id());
    }

    uint32_t toUint() const { return std::bit_cast<uint32_t>(*this); }

    bool operator==(const DeclarationValue&) const = default;

private:
    uint32_t idBits : (32 - CONSTANT_KIND_BITS);
    uint32_t kindBits : CONSTANT_KIND_BITS;
};
inline constexpr DeclarationValue INVALID_DECLARATION_VALUE = { DeclarationValueKind::Invalid, MAX_CONSTANT_ID };

enum class ExpressionKind : uint8_t {
    FirstNonConstantKind = 128,

    ParameterReference = FirstNonConstantKind,
    GlobalReference$Program,
    GlobalReference$Parameterize, // non-dependent
    TemplateParameterReference,
    VariableReference,
    ReferenceReference,
    MemberExpression,
    Call,
    ImplicitCopy,

    LazyParameterize,

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
    static constexpr Expression returnValueReference(FunctionProgram* prog);
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

    constexpr int_t variableIndex() const {
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
    Constant referencedGlobal() const {
        if (kind() == ExpressionKind::GlobalReference$Program)
            return Constant(ConstantKind::Program, id());
        if (kind() == ExpressionKind::GlobalReference$Parameterize)
            return Constant(ConstantKind::Parameterize, id());
        VERIFY_NOT_REACHED();
    }

    bool isReferenceToStaticObject() const {
        return kind() == ExpressionKind::GlobalReference$Program
            || kind() == ExpressionKind::GlobalReference$Parameterize
            || kind() == ExpressionKind::TemplateParameterReference;
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
        (Expression&)* this = other;
        (Expression&)other = INVALID_EXPRESSION;
        return *this;
    }
    constexpr ~OwnedExpression() {
        VERIFY(!isInstructionResult());
    }

    Expression release() {
        Expression ret = *this;
        (Expression&)* this = INVALID_EXPRESSION;
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
struct optional_traits<sema::DeclarationValue> {
    static constexpr sema::DeclarationValue empty_value = sema::INVALID_DECLARATION_VALUE;
};
template<>
struct optional_traits<sema::Expression> {
    static constexpr sema::Expression empty_value = sema::INVALID_EXPRESSION;
};