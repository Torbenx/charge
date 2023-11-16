#pragma once

#include "expr.h"

#define ENUMERATE_INSTRUCTIONS              \
    INST(StaticVariableId, unary)           \
    INST(ForeignConstant, foreign_constant) \
    INST(Load, unary)                       \
    INST(Nop, unary)                        \
    INST(Call, call)                        \
    INST(Allocate, unary)                   \
    INST(Deallocate, unary)                 \
    INST(Store, binary)

enum class Opcode : uint16_t {

#define INST(name, layout) name,
    ENUMERATE_INSTRUCTIONS
#undef INST

};

struct Type : SSAName {
    using SSAName::SSAName;
    explicit Type(SSAName value)
        : SSAName(value) { }
};
struct PureValue : SSAName {
    Type m_type;
    Type type() const { return m_type; }
};
enum class ValueCategory {
    Invalid,
    // p-value: A pure value that directly corrisponds to a SSAName. Must be of value type.
    PValue,
    // l-value: A value with substance. The (pure) value is obtained by loading from the substance.
    LValue,
    // r-value: Like l-value but the substance is owned by the value. Anyone that uses an r-value
    //          must be sure to deallocate the substance when done with the value. 
    RValue,
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
        return { (ValuePhase)typePhase, typeId };
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

    Value(ValueCategory category, SSAName primary, Type type)
        : primaryId(primary.id())
        , typeId(type.id())
        , primaryPhase(std::to_underlying(primary.phase()))
        , typePhase(std::to_underlying(type.phase()))
        , m_category(std::to_underlying(category)) { }
    Value(PureValue value)
        : Value(ValueCategory::PValue, value, value.type()) { }
    static Value lvalue(SSAName substance, Type type) {
        return Value(ValueCategory::LValue, substance, type);
    }
    static Value rvalue(SSAName substance, Type type) {
        return Value(ValueCategory::RValue, substance, type);
    }
};

struct SemanticContext {
    struct ErrorHandler {
        virtual Value unresolvedIdentifier() = 0;
    };
    struct Instrumenter { };

    ErrorHandler* errorHandler = nullptr;
    Instrumenter* instrumenter = nullptr;

    WordStringTable* wordTable = nullptr;

    id<Decl> lookupFromInside(Decl*, WordAndLocation);

    void check(StaticDeclContext*);
    void requireSignature(Decl*);
    void signatureCheckTypeDecl(TypeDecl&);
    void signatureCheckStaticVariableDecl(StaticVariableDecl&);
    void signatureCheckFunctionDecl(FunctionDecl&);
    void checkBody(FunctionDecl&);
};

void dump(Decl*, WordStringTable&);