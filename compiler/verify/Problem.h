#pragma once

#include <verify/ExpressionStorage.h>

#include <ranges>

namespace verify {

struct CallFrame {
};

struct PhiParentInfo {
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
            PhiParentList parents;
        } phi;
    } u;
};

struct Problem : ExpressionStorage {
    struct TypeDeclaration {

        struct Impl {
            DContext dcontext;
            ExprList m_arguments;
            MemberList members;
            std::vector<std::string_view> memberNames;

            DExprList arguments() const { return { m_arguments, dcontext }; }
        };

        std::string_view name;
        std::vector<Impl> impls;
        DContext dcontext;
    };

    TypeDecl addTypeDecl(std::string_view name);

    CodePos currentPos() const { return CodePos { (uint32_t)code.size() }; }

private:
    template<typename T>
    static ListBase makeListInternal(std::vector<T>& vec, std::span<const T> list) {
        uint32_t offset = vec.size();
        vec.insert(vec.end(), list.begin(), list.end());
        return { offset, (uint32_t)list.size() };
    }

    template<typename T>
    static std::span<const T> viewInternal(const std::vector<T>& vec, ListBase list) {
        VERIFY((int_t)list.m_offset + (int_t)list.m_size <= (int_t)vec.size());
        return { vec.data() + list.m_offset, list.m_size };
    }

    std::vector<Instruction> code;
};

}