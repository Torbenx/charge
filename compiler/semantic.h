#pragma once

#include "expr.h"

#define ENUMERATE_INSTRUCTIONS              \
    INST(StaticVariableId, unary)           \
    INST(ForeignConstant, foreign_constant) \
    INST(Load, unary)                       \
    INST(Nop, unary)                        \
    INST(Call, call)

enum class Opcode : uint16_t {

#define INST(name, layout) name,
    ENUMERATE_INSTRUCTIONS
#undef INST

};

struct TypedValue : SSAName {
    SSAName m_type;
    SSAName type() const { return m_type; }
};
enum class ExprValueKind {
    Statement,
    Value,
    Load,
};
struct ExprValue {
    uint16_t primaryId;
    uint16_t typeId;
    uint16_t primaryPhase : 2;
    uint16_t typePhase : 2;
    uint16_t m_kind : 2;

    ExprValueKind kind() const { return (ExprValueKind)m_kind; }
    bool isStatement() const { return kind() == ExprValueKind::Statement; }
    bool valid() const { return !isStatement(); }
    SSAName type() const {
        VERIFY(valid());
        return { (ValuePhase)typePhase, typeId };
    }
    SSAName primary() const {
        VERIFY(valid());
        return { (ValuePhase)primaryPhase, primaryId };
    }
    bool isLiteral() const {
        return kind() == ExprValueKind::Value && primary().phase() == ValuePhase::Literal;
    }
    TypedValue asValue() const {
        VERIFY(kind() == ExprValueKind::Value);
        return { primary(), type() };
    }

    ExprValue(ExprValueKind kind, SSAName primary, SSAName type)
        : primaryId(primary.id())
        , typeId(type.id())
        , primaryPhase(std::to_underlying(primary.phase()))
        , typePhase(std::to_underlying(type.phase()))
        , m_kind(std::to_underlying(kind)) { }
    ExprValue(TypedValue value)
        : ExprValue(ExprValueKind::Value, value, value.type()) { }
    static ExprValue statement() {
        return ExprValue(ExprValueKind::Statement, {}, {});
    }
    static ExprValue load(SSAName substance, SSAName type) {
        return ExprValue(ExprValueKind::Load, substance, type);
    }
};

struct SemanticContext {
    struct ErrorHandler {
        virtual ExprValue unresolvedIdentifier() = 0;
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