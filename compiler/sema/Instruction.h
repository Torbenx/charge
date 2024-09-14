#pragma once

#include <sema/Value.h>

namespace sema {

#define ENUMERATE_SEMA_INSTRUCTION_OPCODES               \
    OP(Reference, LValue)                                \
    OP(LMemberAccess, LValue)                            \
    OP(Constant, PValue)                                 \
    OP(Purify, PValue) /* RValue -> PValue */            \
    OP(Call, RValue)                                     \
    OP(RMemberAccess, RValue)                            \
    OP(ImplicitCopy, RValue) /* LValue -> RValue */      \
    OP(PureInstantiation, RValue) /* PValue -> RValue */ \
    OP(LetDecl, Control)                                 \
    OP(CompoundStmt, Control)                            \
    OP(Function, Control)                                \
    OP(JumpIf, Control)                                  \
    OP(Jump, Control)                                    \
    OP(EndScope, Control)                                \
    OP(Discard, Control)                                 \
    OP(ExpressionHeader, Header)                         \
    OP(FunctionHeader, Header)

enum class Opcode : uint8_t {
#define OP(opcode, cat) opcode,
    ENUMERATE_SEMA_INSTRUCTION_OPCODES
#undef OP
};
std::string_view nameString(Opcode opcode);

enum class InstructionCategory : uint8_t {
    LValue,
    RValue,
    PValue,
    Control,
    Header,
};

inline InstructionCategory categoryOf(Opcode opcode) {
    switch (opcode) {
#define OP(opcode, cat)  \
    case Opcode::opcode: \
        return InstructionCategory::cat;

        ENUMERATE_SEMA_INSTRUCTION_OPCODES
#undef OP

    default:
        VERIFY_NOT_REACHED();
    }
}

inline bool isExpression(Opcode opcode) {
    return categoryOf(opcode) <= InstructionCategory::PValue;
}

union ExpressionData {
    Value constant;
    Value callTarget;
    Value memberPointer;
    uint32_t referencedLocalIndex;

    struct {
    } empty;
};

union InstructionData {
    uint32_t blockSize;
    uint32_t jumpDistance;

    struct {
        Type type;
        ExpressionData u;
    } expr;

    struct {
        Type type;
        uint32_t localValueIndex;
    } decl;

    struct {
    } empty;
};

struct Instruction {
    Instruction(Opcode op, SourceLocation location, InstructionData data)
        : m_location(op, location), u(data) { }

    Opcode opcode() const { return m_location.tag(); }
    SourceLocation location() const { return m_location.location(); }

    TaggedSourceLocation<Opcode> m_location;
    InstructionData u;
};

}