#pragma once

#include <types.h>

#include <ranges>
#include <utility>

namespace verify {

enum class Sort : uint8_t {
    Bool,
    Type,
    Member,
    MemoryDecl,
    MemoryLoc,
};

enum class ExprKind : uint8_t {
#define EXPR(kind, ...) kind,
#include <verify/expressions.inc>
};

struct Expr {
    Expr(ExprKind kind, uint32_t id)
        : kindBits(std::to_underlying(kind)), idBits(id) { }

    ExprKind kind() const { return (ExprKind)kindBits; }
    uint32_t id() const { return idBits; }
    uint32_t kindBits : 7 = 0;
    uint32_t boolNegatedBit : 1 = 0;
    uint32_t idBits : 24 = 0;

    bool operator==(const Expr&) const;
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
};
struct ExprList : ListBase { };
struct FrameList : ListBase { };
struct ParentList : ListBase { };
struct SortList : ListBase { };

template<typename T>
struct D : T {
    SortList pSorts;
    D(T t, SortList pSorts)
        : T(t), pSorts(pSorts) { }
    operator D<Expr>() const
        requires std::is_base_of_v<Expr, T>
    {
        return D<Expr>((const T&)*this, pSorts);
    }
};

using DExprList = D<ExprList>;
using DExpr = D<Expr>;
using DBool = D<Bool>;
using DType = D<Type>;
using DMember = D<Member>;
using DMemoryDecl = D<MemoryDecl>;
using DMemoryLoc = D<MemoryLoc>;

struct TypeDecl {
    explicit TypeDecl(uint32_t id)
        : m_id(id) { }
    uint32_t id() const { return m_id; }
    uint32_t m_id;
};
using DTypeDecl = TypeDecl; // Type declaration cannot be dependent

// Represents the code position just before execution of 'instruction'
struct CodePos {
    uint32_t instruction;
};
using DCodePos = CodePos; // Code positions cannot be dependent

}