#pragma once

#include <verify/ir/ExpressionStorage.h>

#include <ranges>

namespace verify::ir {

struct TypeBuilder;
struct FnBuilder;

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

struct Function : ExpressionStorage {

    FnHandle repr(Fn expr);

    const Instruction& inst(CodePos pos) const {
        if (pos.simple()) {
            VERIFY(pos.offset() < m_instructions.size());
            return m_instructions[pos.offset()];
        } else {
            VERIFY(pos.dataBits < m_complexPositions.size());
            return inst(m_complexPositions[pos.dataBits].simplePos);
        }
    }

    Bool prop(Theorem t) const {
        VERIFY(t.id() < m_theorems.size());
        return m_theorems[t.id()].prop;
    }
    Proof proof(Theorem t) const {
        VERIFY(t.id() < m_theorems.size());
        return m_theorems[t.id()].proof;
    }
    CodePos position(Theorem t) const {
        VERIFY(t.id() < m_theorems.size());
        return m_theorems[t.id()].pos;
    }

private:
    struct TheoremData {
        Bool prop;
        Proof proof;
        CodePos pos;
    };

    struct ComplexPosData {
        std::vector<PhiParent> backedges;
        CodePos simplePos;
    };

    std::vector<Instruction> m_instructions;
    std::vector<TheoremData> m_theorems;
    std::vector<ComplexPosData> m_complexPositions;
};

struct Database : ExpressionStorage {

    DeclHandle newDecl(DeclHandle parent) {
        uint32_t id = declarations.size();
        declarations.push_back({ .parent = parent, .children = {} });
        declarations[parent.id()].children.push_back(DeclHandle(id));
        return DeclHandle(id);
    }

private:
    struct MemberLit {
        Type type;
    };

    struct DeclarationNode {
        DeclHandle parent;
        std::vector<DeclHandle> children;
    };

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

    std::vector<MemberLit> memberLiterals;
    std::vector<DeclarationNode> declarations;
};

}