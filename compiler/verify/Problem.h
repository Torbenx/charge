#pragma once

#include <verify/ExpressionStorage.h>

namespace verify {

struct CallFrame {
};

struct PhiParent {
    Bool condition;
    CodePos pos;
};

enum class Opcode : uint8_t {
    Store,
    Call,
    Phi,
};

struct Instruction {
    Opcode opcode() const { return m_opcode; }
    auto store() const {
        VERIFY(opcode() == Opcode::Store);
        return u.store;
    }
    auto call() const {
        VERIFY(opcode() == Opcode::Call);
        return u.call;
    }
    auto phi() const {
        VERIFY(opcode() == Opcode::Phi);
        return u.phi;
    }

    Opcode m_opcode;
    union {
        struct {
            MemoryLoc loc;
            Expr expr;
        } store;
        struct {
            FrameList frames;
        } call;
        struct {
            ParentList parents;
        } phi;
    } u;
};

struct Problem : ExpressionStorage {
    struct TypeDeclaration {
        struct Impl {
            ExprList expressions;
            std::vector<Member> members;
        };

        std::string_view name;
        std::vector<Impl> impls;
        SortList parameterSorts;
    };

    TypeDecl addTypeDecl(std::string_view name);

    CodePos currentPos() const { return CodePos { (uint32_t)code.size() }; }

    std::vector<Instruction> code;
};

}