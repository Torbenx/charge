#pragma once

#include "expr.h"

#define ENUMERATE_INSTRUCTIONS \
    INST(StaticVariableId, unary)

enum class Opcode : uint16_t {

#define INST(name, layout) name,
    ENUMERATE_INSTRUCTIONS
#undef INST

};

struct LookupContext {
};

// The value of an expression is either a
//  - value (SSAName),
//  - literal, or
//  - hypothetical value obtained from a
//     - load, or
//     - function call.
// For values of unknown type we must enforce the stricter object semantics.
struct LiteralValue {
    SSAName literal;
};
struct LoadValue {
    SSAName substance;
};
struct CallValue {
};
struct ExprValue {
    std::variant<LiteralValue, LoadValue, CallValue> value;
    SSAName type;
};

struct SemanticContext {
    struct ErrorHandler {
        virtual ExprValue unresolvedIdentifier() = 0;
    };
    struct Instrumenter { };

    ErrorHandler* errorHandler = nullptr;
    Instrumenter* instrumenter = nullptr;

    std::optional<id<Decl>> performLookup(LookupContext& ctx, Word name, LocalSourceRange dbgRange);

    void signatureCheckStaticVariableDecl(StaticVariableDecl&);
};