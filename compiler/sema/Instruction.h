#pragma once

#include <sema/Constant.h>

namespace sema {

#define ENUMERATE_SEMA_INSTRUCTION_OPCODES \
    OP(Call)                               \
    OP(Branch)                             \
    OP(BranchContinued)                    \
    OP(Discard)                            \
    OP(Deactivate)                         \
    OP(Initialize)                         \
    OP(BlockScope)                         \
    OP(EndScope)

enum class Opcode : uint8_t {
#define OP(opcode) opcode,
    ENUMERATE_SEMA_INSTRUCTION_OPCODES
#undef OP
};
std::string_view nameString(Opcode opcode);

struct Instruction {
    union ScopeData {
        Expression branchCondition;

        struct {
        } empty;
    };

    union Data {
        Expression callExpression;
        Expression discardValue;
        Expression deactivateTarget;
        struct {
            Expression target;
            Expression value;
        } initialize;
        struct {
            uint32_t bodySize = std::numeric_limits<uint32_t>::max();
            ScopeData u;
        } scope;

        struct {
        } empty;
    };

    Instruction(Opcode op, SourceLocation location, Data data)
        : m_location(op, location), u(data) { }

    Opcode opcode() const { return m_location.tag(); }
    SourceLocation location() const { return m_location.location(); }

    TaggedSourceLocation<Opcode> m_location;

    Data u;
};
static_assert(sizeof(Instruction) == 16);

}