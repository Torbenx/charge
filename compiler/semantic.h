#pragma once

#include "expr.h"

struct SSAName {
    uint32_t id;
};

struct Inst {
    uint16_t opcode;
    uint16_t length;
};
struct ValueInst : Inst {
    SSAName result;
};
struct DeclarationReference : ValueInst {
    uint32_t decl;
};

struct BasicBlock {

};

struct LookupContext {

};

// The value of an expression is either a
//  - value (SSAName),
//  - literal, or
//  - hypothetical value obtained from a
//     - load, or
//     - function call.
// There should be no values of unknown or object type.
// For values of unknown type we must enforce the stricter object semantics.
struct DeclLiteral {

};
struct LoadValue {

};
struct CallValue {

};
struct ExprValue {
    std::variant<DeclLiteral, LoadValue, CallValue> value;
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
};