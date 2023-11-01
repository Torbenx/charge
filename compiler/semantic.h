#pragma once

#include "expr.h"

#define ENUMERATE_INSTRUCTIONS           \
    INST(StaticVariableId, unary)        \
    INST(ForeignConstant, foreign_const) \
    INST(Load, unary)                    \
    INST(Nop, unary)                     \
    INST(Call, call)

enum class Opcode : uint16_t {

#define INST(name, layout) name,
    ENUMERATE_INSTRUCTIONS
#undef INST

};

// The value of an expression is either a
//  - value (SSAName), or
//  - hypothetical value obtained from a
//     - load, or
//     - function call.
// For values of unknown type we must enforce the stricter object semantics.
struct LoadValue {
    SSAName substance;
};
struct TypedValue : SSAName {
    SSAName type;
};
struct ExprValue {
    std::variant<SSAName, LoadValue> value;
    SSAName type;

    TypedValue asValue() const {
        return { std::get<0>(value), type };
    }
};

struct SemanticContext {
    struct ErrorHandler {
        virtual ExprValue unresolvedIdentifier() = 0;
    };
    struct Instrumenter { };

    ErrorHandler* errorHandler = nullptr;
    Instrumenter* instrumenter = nullptr;

    id<Decl> lookupFromInside(Decl*, WordAndLocation);

    void check(StaticDeclContext*);
    void requireSignature(Decl*);
    void signatureCheckTypeDecl(TypeDecl&);
    void signatureCheckStaticVariableDecl(StaticVariableDecl&);
    void signatureCheckFunctionDecl(FunctionDecl&);
    void checkBody(FunctionDecl&);

};