#include <sema/Instruction.h>

namespace sema {

std::string_view nameString(Opcode opcode) {

    switch (opcode) {
#define OP(name)  \
    case Opcode::name: \
        return #name;
        ENUMERATE_SEMA_INSTRUCTION_OPCODES
#undef OP

    default:
        VERIFY_NOT_REACHED();
    }
}

}