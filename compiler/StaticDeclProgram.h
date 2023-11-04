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
        : InstructionOperand(false, MAX_ID) { }

    bool constant() const { return encoded & (uint16_t)0x8000; }
    uint16_t id() const { return encoded & (uint16_t)0x7fff; }

    constexpr bool operator==(const InstructionOperand&) const = default;
};
template<>
struct optional_traits<InstructionOperand> {
    static constexpr InstructionOperand empty_value = {};
};
struct ConstantStreamInstructionOperand : InstructionOperand {
    ValuePhase phase() const { return constant() ? ValuePhase::Literal : ValuePhase::Constant; }
    constexpr bool operator==(const ConstantStreamInstructionOperand&) const = default;
};
template<>
struct optional_traits<ConstantStreamInstructionOperand> {
    static constexpr ConstantStreamInstructionOperand empty_value = {};
};

struct Instruction {
    static constexpr InstructionOperand UNUSED_OPERAND = {};
    uint64_t op : 16;
    uint64_t a : 16;
    uint64_t b : 16;
    uint64_t c : 16;

    constexpr Instruction(Opcode op, InstructionOperand a, InstructionOperand b, InstructionOperand c)
        : op(std::to_underlying(op)), a(a.encoded), b(b.encoded), c(c.encoded) { }
    constexpr Instruction(Opcode op, InstructionOperand a, InstructionOperand b, uint16_t c)
        : op(std::to_underlying(op)), a(a.encoded), b(b.encoded), c(c) { }

    Opcode opcode() const { return (Opcode)op; }
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
    constexpr InstructionOperand localize(ValuePhase targetPhase) const {
        if (phase() == targetPhase)
            return { false, id() };
        if (std::to_underlying(phase()) + 1 == std::to_underlying(targetPhase))
            return { true, id() };
        VERIFY_NOT_REACHED();
    }
    constexpr ConstantStreamInstructionOperand localizeConstant() const {
        return { localize(ValuePhase::Constant) };
    }
};

struct InstructionStream {
    std::vector<uint16_t> definitions;
    std::vector<Instruction> stream;
    ValuePhase stream_phase;

    constexpr InstructionStream(ValuePhase phase)
        : stream_phase(phase) { }

    template<Opcode op, typename... Args>
    auto emit(Args... args);

    InstructionOperand localize(SSAName name) const {
        return name.localize(stream_phase);
    }

private:
    // Must only be called directly before emitting an instruction into the stream.
    SSAName allocateName();
    SSAName emit_unary(Opcode op, SSAName in);
    SSAName emit_binary(Opcode op, SSAName in1, SSAName in2);
    SSAName emit_foreign_constant(Opcode op, SSAName decl, ConstantStreamInstructionOperand constant);
    SSAName emit_call(Opcode op, SSAName argsBase, uint16_t count);
};

enum class ConstantType : uint8_t {
    Decl,
};
struct TypedConstant {
    ConstantType type;
    uint64_t encodedValue;

    TypedConstant(ConstantType type, uint64_t encodedValue)
        : type(type), encodedValue(encodedValue) { }
    TypedConstant(Decl* decl)
        : type(ConstantType::Decl), encodedValue((uintptr_t)decl) { }

    Decl* asDecl() const {
        VERIFY(type == ConstantType::Decl);
        return (Decl*)(uintptr_t)encodedValue;
    }

    bool operator==(const TypedConstant& other) const = default;
};
constexpr bool compareConstantsOfSameType(const TypedConstant& left, const TypedConstant& right) {
    VERIFY(left.type == right.type);
    return left.encodedValue == right.encodedValue;
}
struct ConstantTable {
    std::vector<uint64_t> encodedValues;
    std::vector<ConstantType> types;
    ValuePhase table_phase;

    constexpr ConstantTable(ValuePhase phase)
        : table_phase(phase) { }

    SSAName emit(TypedConstant);
    TypedConstant get(uint16_t index) const {
        return { types[index], encodedValues[index] };
    }
};

struct StaticDeclProgram {
    ConstantTable literalTable { ValuePhase::Literal };
    InstructionStream constantStream { ValuePhase::Constant };
    InstructionStream runtimeStream { ValuePhase::Runtime };
};