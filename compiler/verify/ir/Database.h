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

struct Database : ExpressionStorage {
    struct TypeImplementation {
        TypeDecl declaration;
        DContext dcontext;

        DExprList arguments() const { return { dcontext, m_arguments }; }

    private:
        ExprList m_arguments;
        MemberLiteralList m_members;
        friend Database;
    };

    struct TypeDeclaration {
        TypeImplList impls;
        DContext dcontext;
    };

    struct FnImplementation {
        FnDecl declaration;
        DContext dcontext;

        DExprList arguments() const { return { dcontext, m_arguments }; }

    private:
        ExprList m_arguments;
        friend Database;
    };

    struct FnDeclaration {
        FnImplList impls;
        DContext context;
    };

    const TypeDeclaration& at(TypeDecl decl) {
        VERIFY(decl.id() < typeDecls.size());
        return typeDecls[decl.id()];
    }
    const TypeImplementation& at(TypeImpl impl) {
        VERIFY(impl.id() < typeImpls.size());
        return typeImpls[impl.id()];
    }
    std::span<const TypeImplementation> view(TypeImplList list) {
        return viewInternal(typeImpls, list);
    }

    DType typeOf(TypeImpl impl, MemberLiteral literal) {
        VERIFY(at(impl).m_members.contains(literal));
        return { at(impl).dcontext, memberLiterals[literal.id()].type };
    }

private:
    struct MemberLit {
        Type type;
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

    std::vector<TypeDeclaration> typeDecls;
    std::vector<TypeImplementation> typeImpls;
    std::vector<MemberLit> memberLiterals;
};

}