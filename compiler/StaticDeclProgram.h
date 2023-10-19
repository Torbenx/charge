#pragma once

#include "types.h"
#include <utility>
#include <vector>

struct Decl;

enum class Opcode : uint16_t;

enum class ValuePhase : uint8_t {
    Literal = 0,
    Constant = 1,
    Runtime = 2,
};

struct InstructionOperand {
    static constexpr uint16_t MAX_ID = 0x7fff;
    uint16_t encoded;
    // constant: Is this a constant from the prespective of the stream
    //           the instruction is in?
    constexpr InstructionOperand(bool constant, uint16_t id)
        : encoded(id | (constant ? (uint16_t)0x8000 : (uint16_t)0)) { }
    constexpr InstructionOperand()
        : InstructionOperand(true, MAX_ID) { }
};

struct Instruction {
    static constexpr InstructionOperand UNUSED_OPERAND = {};
    uint64_t op : 16;
    uint64_t a : 16;
    uint64_t b : 16;
    uint64_t c : 16;

    constexpr Instruction(Opcode op, InstructionOperand a, InstructionOperand b, InstructionOperand c)
        : op(std::to_underlying(op)), a(a.encoded), b(b.encoded), c(c.encoded) { }
};

struct SSAName {
    ValuePhase m_phase;
    uint16_t m_id;

    constexpr SSAName()
        : m_phase(ValuePhase::Literal), m_id(0) { }
    constexpr SSAName(ValuePhase phase, size_t id)
        : m_phase(phase), m_id(id) { VERIFY(id <= InstructionOperand::MAX_ID); }

    constexpr ValuePhase phase() const { return m_phase; }
    constexpr uint16_t id() const { return m_id; }
};

struct InstructionStream {
    std::vector<uint16_t> definitions;
    std::vector<Instruction> stream;
    ValuePhase stream_phase;

    constexpr InstructionStream(ValuePhase phase)
        : stream_phase(phase) { }

    template<Opcode op, typename... Args>
    auto emit(Args... args);

private:
    // Must only be called directly before emitting an instruction into the stream.
    SSAName allocateName();
    InstructionOperand localize(SSAName name) const;
    SSAName emit_unary(Opcode op, SSAName in);
};

struct ValueTable {
    std::vector<uint64_t> values;
    ValuePhase table_phase;

    constexpr ValueTable(ValuePhase phase)
        : table_phase(phase) { }

    SSAName emit(uint64_t val);
    SSAName emit(Decl* decl);
};

struct StaticDeclProgram {
    ValueTable literalTable { ValuePhase::Literal };
    InstructionStream constantStream { ValuePhase::Constant };
    InstructionStream runtimeStream { ValuePhase::Runtime };
};