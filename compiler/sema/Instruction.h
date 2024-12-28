#pragma once

#include <sema/Constant.h>

namespace sema {

#define ENUMERATE_SEMA_INSTRUCTION_OPCODES \
    OP(Call)                               \
    OP(Branch)                             \
    OP(Discard)                            \
    OP(Deactivate)                         \
    OP(ImplicitCopy)                       \
    OP(Initialize)                         \
    OP(Compound)

enum class Opcode : uint8_t {
#define OP(opcode) opcode,
    ENUMERATE_SEMA_INSTRUCTION_OPCODES
#undef OP
};
std::string_view nameString(Opcode opcode);

struct Instruction {
    Instruction(Opcode op, SourceLocation location)
        : m_location(op, location) { }

    Opcode opcode() const { return m_location.tag(); }
    SourceLocation location() const { return m_location.location(); }

    TaggedSourceLocation<Opcode> m_location;
};

struct CallInstruction : Instruction {
    CallInstruction(SourceLocation location, int_t argumentCount)
        : Instruction(Opcode::Call, location), m_arguments(argumentCount, INVALID_EXPRESSION_RESULT) { }

    std::span<const ExpressionResult> arugments() const { return m_arguments; }

    void setCall(Constant callTarget, Type returnType) {
        VERIFY(this->callTarget == INVALID_CONSTANT);
        this->callTarget = callTarget;
        this->returnType = returnType;
    }

    void setArgument(int_t index, OwnedExpressionResult value) {
        VERIFY(index < (int_t)m_arguments.size());
        VERIFY(m_arguments[index] == INVALID_EXPRESSION_RESULT);
        m_arguments[index] = value.release();
    }

    Constant callTarget = INVALID_CONSTANT;
    Type returnType = (Type)INVALID_CONSTANT;
    std::vector<ExpressionResult> m_arguments;
};

struct ImplicitCopyInstruction : Instruction {
    ImplicitCopyInstruction(SourceLocation location, Reference copyFrom)
        : Instruction(Opcode::ImplicitCopy, location), copyFrom(copyFrom) { }

    Reference copyFrom;
};

struct DiscardInstruction : Instruction {
    DiscardInstruction(SourceLocation location, OwnedExpressionResult value)
        : Instruction(Opcode::Discard, location), value(value.release()) { }

    ExpressionResult value;
};

//! Destory variables, discard references
struct DeactivateInstruction : Instruction {
    DeactivateInstruction(SourceLocation location, Reference target)
        : Instruction(Opcode::Deactivate, location), target(target) { }

    Reference target;
};

struct InitializeInstruction : Instruction {
    InitializeInstruction(SourceLocation location, Reference target, OwnedExpressionResult initializer)
        : Instruction(Opcode::Initialize, location), target(target), initializer(initializer.release()) { }

    Reference target;
    ExpressionResult initializer;
};

struct LocalScope {
    struct VariableDeclaration {
        SourceLocation location() const { return m_location; }

        SourceLocation m_location;
        Type type;
    };
    struct ReferenceDeclaration {
        SourceLocation location() const { return m_location.location(); }
        ExpressionCategory category() const { return m_location.tag(); }

        TaggedSourceLocation<ExpressionCategory> m_location;
        Type type;
    };

    LocalScope() = default;
    explicit LocalScope(std::vector<Instruction*> body)
        : m_body(std::move(body)) { }

    void setBody(std::vector<Instruction*> body) {
        m_body = std::move(body);
    }
    std::span<Instruction* const> body() const { return m_body; }

    std::vector<VariableDeclaration> variableDeclarations;
    std::vector<ReferenceDeclaration> referenceDeclarations;
    std::vector<Instruction*> m_body;
};

struct BranchInstruction : Instruction {
    struct Branch : LocalScope {
        Branch(LocalScope scope, ExpressionResult condition)
            : LocalScope(std::move(scope)), condition(condition) { }
        ExpressionResult condition;
    };

    BranchInstruction(SourceLocation location)
        : Instruction(Opcode::Branch, location) { }

    Branch& addBranch(LocalScope scope, OwnedExpressionResult condition) {
        return m_branches.emplace_back(std::move(scope), condition.release());
    }

    std::span<const Branch> branches() const { return m_branches; }

    std::vector<Branch> m_branches;
};

struct CompoundInstruction : Instruction, LocalScope {
    CompoundInstruction(SourceLocation location, LocalScope scope)
        : Instruction(Opcode::Compound, location), LocalScope(std::move(scope)) { }
};

struct LoopInstruction : Instruction, LocalScope {
    ExpressionResult latchCondition;
};

template<typename T>
constexpr std::optional<T*> try_cast(Instruction* inst) {
    switch (inst->opcode()) {

#define OP(opcode)                                               \
    case Opcode::opcode:                                         \
        if constexpr (std::derived_from<opcode##Instruction, T>) \
            return static_cast<opcode##Instruction*>(inst);      \
        else                                                     \
            return std::nullopt;

        ENUMERATE_SEMA_INSTRUCTION_OPCODES

#undef OP

    default:
        VERIFY_NOT_REACHED();
    }
}

template<typename T>
constexpr T* cast(Instruction* inst) { return try_cast<T>(inst).value(); }

}