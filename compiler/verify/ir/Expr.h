#pragma once

#include <types.h>

#include <ranges>
#include <utility>

namespace verify::ir {

enum class Sort : uint8_t {
    Bool,
    Type,
    Member,
    MemoryDecl,
    MemoryLoc,
};

enum class ExprKind : uint8_t {
#define EXPR(kind, ...) kind,
#include <verify/ir/expressions.inc>
};

struct Expr {
    Expr(ExprKind kind, uint32_t id)
        : kindBits(std::to_underlying(kind)), idBits(id) { }

    ExprKind kind() const { return (ExprKind)kindBits; }
    uint32_t id() const { return idBits; }
    uint32_t kindBits : 7 = 0;
    uint32_t boolNegatedBit : 1 = 0;
    uint32_t idBits : 24 = 0;

    bool operator==(const Expr&) const = default;
};

struct Bool : Expr {
    explicit Bool(Expr e)
        : Expr(e) { }
    explicit Bool(bool literal)
        : Expr(ExprKind::FalseLiteral, 0) { boolNegatedBit = literal; }

    Bool operator!() const {
        Bool copy = *this;
        copy.boolNegatedBit = !boolNegatedBit;
        return copy;
    }
};

struct Type : Expr {
    explicit Type(Expr e)
        : Expr(e) { }
};
struct MemoryDecl : Expr {
    explicit MemoryDecl(Expr e)
        : Expr(e) { }
};
struct Member : Expr {
    explicit Member(Expr e)
        : Expr(e) { }
};

struct MemoryLoc : Expr {
    explicit MemoryLoc(Expr e)
        : Expr(e) { }
};

struct ListBase {
    uint32_t m_offset = 0;
    uint32_t m_size = 0;

    bool empty() const { return m_size == 0; }
    int_t size() const { return m_size; }
};
struct ExprList : ListBase { };
struct FrameList : ListBase { };
struct SortList : ListBase { };

struct SmallHandle {
    explicit SmallHandle(uint32_t id)
        : m_id(id) { }
    uint32_t id() const { return m_id; }
    bool operator==(const SmallHandle&) const = default;

private:
    uint32_t m_id;
};

template<std::derived_from<SmallHandle> T>
struct SmallHandleList : ListBase {
    T at(int_t index) const {
        VERIFY(index < ListBase::size());
        return T { this->m_offset + (uint32_t)index };
    }
    bool contains(T t) const {
        int_t diff = t.id() - this->m_offset;
        return (size_t)diff < (size_t)this->m_size;
    }
};

struct PhiParent : SmallHandle {
    using SmallHandle::SmallHandle;
};
using PhiParentList = SmallHandleList<PhiParent>;

struct MemberLiteral : SmallHandle {
    using SmallHandle::SmallHandle;
};
using MemberLiteralList = SmallHandleList<MemberLiteral>;

struct DContext : SmallHandle {
    using SmallHandle::SmallHandle;
};

template<typename T>
struct D : T {
    DContext dctx;
    D(DContext dctx, T t)
        : T(t), dctx(dctx) { }
    operator D<Expr>() const
        requires std::is_base_of_v<Expr, T>
    {
        return D<Expr>((const T&)*this, dctx);
    }
};

using DExprList = D<ExprList>;
using DExpr = D<Expr>;
using DBool = D<Bool>;
using DType = D<Type>;
using DMember = D<Member>;
using DMemoryDecl = D<MemoryDecl>;
using DMemoryLoc = D<MemoryLoc>;

struct TypeDecl : SmallHandle {
    using SmallHandle::SmallHandle;
};
using DTypeDecl = TypeDecl; // Not dependent

struct TypeImpl : SmallHandle {
    using SmallHandle::SmallHandle;
};
using DTypeImpl = TypeImpl; // Not dependent
using TypeImplList = SmallHandleList<TypeImpl>;

// Represents the code position just before the instruction 'id()'
struct CodePos : SmallHandle {
    using SmallHandle::SmallHandle;
};
using DCodePos = CodePos; // Not dependent
using CodeBlock = SmallHandleList<CodePos>;

struct FnDecl : SmallHandle {
    using SmallHandle::SmallHandle;
};
using DFnDecl = FnDecl; // Not dependent

struct FnImpl : SmallHandle {
    using SmallHandle::SmallHandle;
};
using DFnImpl = FnImpl; // Not dependent
using FnImplList = SmallHandleList<FnImpl>;

}