#pragma once

#include <types.h>

#include <ranges>
#include <utility>

namespace verify::ir {

enum class Sort : uint8_t {
#define SORT(name, snake_case) name,
#include <verify/ir/sorts.inc>
    COUNT,
};

enum class ExprKind : uint8_t {
#define EXPR(kind, ...) kind,
#include <verify/ir/expressions.inc>
};

//! The expressions of all sorts that only differ in the sort they are declared with
/*!
Loads exist once per sort. Since all of them store the same data they can be created
and inspected without knowing the sort at compile time.
*/
inline bool isLoad(ExprKind kind) {
    switch (kind) {
#define SORT(name, snake_case) case ExprKind::name##Load:
#include <verify/ir/sorts.inc>
        return true;
    default:
        return false;
    }
}

inline ExprKind loadKind(Sort sort) {
    switch (sort) {
#define SORT(name, snake_case) \
    case Sort::name:           \
        return ExprKind::name##Load;
#include <verify/ir/sorts.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

enum class Opcode : uint8_t {
#define INSTRUCTION(name, ...) name,
#include <verify/ir/instructions.inc>
};

enum class Tactic : uint8_t {
#define TACTIC(name) name,
#include <verify/ir/tactics.inc>
};

struct Proof {
#define SIMPLE_TACTIC(name) \
    static Proof make##name() { return Proof(Tactic::name, 0); }
#include <verify/ir/tactics.inc>

    Proof(Tactic tactic, uint32_t id)
        : tacticBits(std::to_underlying(tactic)), idBits(id) { }

    Tactic tactic() const { return (Tactic)tacticBits; }
    uint32_t id() const { return idBits; }

    uint32_t tacticBits : 8 = 0;
    uint32_t idBits : 24 = 0;
};

struct ListBase {
    uint32_t m_offset = 0;
    uint32_t m_size = 0;

    bool empty() const { return m_size == 0; }
    int_t size() const { return m_size; }
};
struct ExprList : ListBase { };
struct SortList : ListBase { };

struct SmallHandle {
    explicit constexpr SmallHandle(uint32_t id)
        : m_id(id) { }
    constexpr uint32_t id() const { return m_id; }
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

struct Theorem : SmallHandle {
    using SmallHandle::SmallHandle;
};

struct PhiParent : SmallHandle {
    using SmallHandle::SmallHandle;
};
using PhiParentList = SmallHandleList<PhiParent>;

struct MemberLiteral : SmallHandle {
    using SmallHandle::SmallHandle;
};
using MemberLiteralList = SmallHandleList<MemberLiteral>;

struct TypeImpl : SmallHandle {
    using SmallHandle::SmallHandle;
};

enum class RelativePosKind : uint8_t {
    Simple,
    Complex,
};

// Represents the code position just before the instruction at 'id()'.
struct CodePos : SmallHandle {
    using SmallHandle::SmallHandle;
};
inline constexpr CodePos INVALID_CODE_POS = CodePos(limits::max);

struct RelativeCodePos {
    RelativeCodePos(CodePos pos)
        : RelativeCodePos(RelativePosKind::Simple, pos.id()) { }
    RelativeCodePos(RelativePosKind kind, uint32_t data)
        : kindBits(std::to_underlying(kind)), dataBits(data) { }

    RelativePosKind kind() const {
        return (RelativePosKind)kindBits;
    }
    bool simple() const {
        return kind() == RelativePosKind::Simple;
    }
    uint32_t offset() const {
        VERIFY(simple());
        return dataBits;
    }

    uint32_t kindBits : 8 = 0;
    uint32_t dataBits : 24 = 0;
};

struct FnHandle : SmallHandle {
    using SmallHandle::SmallHandle;
};

struct DeclHandle : SmallHandle {
    using SmallHandle::SmallHandle;
};

namespace inline_expr_detail {
    template<typename T>
    constexpr uint32_t toUInt(T arg) {
        if constexpr (std::derived_from<T, SmallHandle>)
            return arg.id();
        else
            return static_cast<uint32_t>(arg);
    }
}

struct Bool;
struct Fn;
struct Type;
struct Member;
struct MemoryDecl;
struct MemoryLoc;

struct Expr {
#define INLINE_EXPR(name, sort, Arg) \
    static sort make##name(Arg);
#include <verify/ir/expressions.inc>

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
    static constexpr Sort sort = Sort::Bool;
    explicit Bool(Expr e)
        : Expr(e) { }
    explicit Bool(bool literal)
        : Expr(ExprKind::BooleanLiteral, 0) { boolNegatedBit = literal; }

    Bool operator!() const {
        Bool copy = *this;
        copy.boolNegatedBit = !boolNegatedBit;
        return copy;
    }
};

struct Fn : Expr {
    static constexpr Sort sort = Sort::Fn;
    explicit Fn(Expr e)
        : Expr(e) { }
};

struct Type : Expr {
    static constexpr Sort sort = Sort::Type;
    explicit Type(Expr e)
        : Expr(e) { }
};
struct MemoryDecl : Expr {
    static constexpr Sort sort = Sort::MemoryDecl;
    explicit MemoryDecl(Expr e)
        : Expr(e) { }
};
struct Member : Expr {
    static constexpr Sort sort = Sort::Member;
    explicit Member(Expr e)
        : Expr(e) { }
};

struct MemoryLoc : Expr {
    static constexpr Sort sort = Sort::MemoryLoc;
    explicit MemoryLoc(Expr e)
        : Expr(e) { }
};

#define INLINE_EXPR(name, sort, Arg)                                        \
    inline sort Expr::make##name(Arg arg) {                                 \
        return (sort)Expr(ExprKind::name, inline_expr_detail::toUInt(arg)); \
    }
#include <verify/ir/expressions.inc>

}

template<std::derived_from<verify::ir::SmallHandle> T>
struct optional_traits<T> {
    static constexpr T empty_value = T(limits::max);
};