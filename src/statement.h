#pragma once

#include "Lexer.h"
#include <concepts>
#include <cstdint>
#include <optional>

// Basics
#define ENUMERATE_DECL_KINDS \
    DECL_KIND(StructDecl)    \
    DECL_KIND(FnDecl)        \
    DECL_KIND(GlobalDecl)    \
    DECL_KIND(LocalDecl)     \
    DECL_KIND(HasDecl)       \
    DECL_KIND(NamespaceDecl) \
    DECL_KIND(EnumDecl)      \
    DECL_KIND(EnumValueDecl)

#define DECL_KIND(kind) kind,
enum class DeclKind : uint8_t {
    Invalid,
    ENUMERATE_DECL_KINDS
};
#undef DECL_KIND
const char* toString(DeclKind);

struct Decl;
#define DECL_KIND(kind) struct kind;
ENUMERATE_DECL_KINDS
#undef DECL_KIND

#define ENUMERATE_STMT_KINDS \
    STMT_KIND(AssignStmt)    \
    STMT_KIND(NullStmt)      \
    STMT_KIND(CompoundStmt)  \
    STMT_KIND(LetStmt)       \
    STMT_KIND(ExprStmt)      \
    STMT_KIND(ReturnStmt)    \
    STMT_KIND(IfStmt)        \
    STMT_KIND(ForStmt)       \
    STMT_KIND(WhileStmt)

#define ENUMERATE_EXPR_KINDS      \
    EXPR_KIND(UnaryOperatorExpr)  \
    EXPR_KIND(ParenExpr)          \
    EXPR_KIND(AccessExpr)         \
    EXPR_KIND(CallExpr)           \
    EXPR_KIND(IdentifierExpr)     \
    EXPR_KIND(BinaryOperatorExpr) \
    EXPR_KIND(IntLiteralExpr)     \
    EXPR_KIND(ConstraintExpr)     \
    EXPR_KIND(CompoundExpr)

#define STMT_KIND(kind) kind,
enum class StmtKind : uint8_t {
    Invalid,
    ENUMERATE_STMT_KINDS
};
#undef STMT_KIND
const char* toString(StmtKind);

#define EXPR_KIND(kind) kind,
enum class ExprKind : uint8_t {
    Invalid,
    ENUMERATE_EXPR_KINDS
};
#undef EXPR_KIND
const char* toString(ExprKind);

struct Stmt;
struct Expr;
#define STMT_KIND(kind) struct kind;
ENUMERATE_STMT_KINDS
#undef STMT_KIND
#define EXPR_KIND(kind) struct kind;
ENUMERATE_EXPR_KINDS
#undef EXPR_KIND

template<typename Derived, typename Base>
inline std::ptrdiff_t computeOffsetInBase() {
    auto base = (const Base*)1;
    auto derived = static_cast<const Derived*>(base);
    return (const std::byte*)derived - (const std::byte*)base;
}

template<typename E>
struct Ptr {
    uint32_t offsetAlign4 = 0;

    Ptr() = default;
    explicit Ptr(uint32_t off)
        : offsetAlign4(off) { }
    template<typename Base>
    explicit Ptr(const Ptr<Base>& p) requires std::derived_from<E, Base>
        : offsetAlign4(p.offsetAlign4 + computeOffsetInBase<E, Base>() / sizeof(uint32_t)) { }

    template<typename Base>
    operator Ptr<Base>() const requires std::derived_from<E, Base> {
        return Ptr<Base> { (uint32_t)(offsetAlign4 - computeOffsetInBase<E, Base>() / sizeof(uint32_t)) };
    }

    explicit operator bool() const { return offsetAlign4 != 0; }
    bool operator==(const Ptr& other) const { return offsetAlign4 == other.offsetAlign4; }
};

template<typename T>
struct Span {
    Ptr<T> begin;
    uint32_t count = 0;

    constexpr Ptr<T> operator[](uint32_t i) const requires(sizeof(T) % sizeof(uint32_t) == 0) {
        return Ptr<T> { begin.offsetAlign4 + i * (uint32_t)(sizeof(T) / sizeof(uint32_t)) };
    }
    template<typename Base>
    explicit operator Span<Ptr<Base>>() const
        requires(std::is_convertible_v<T, Ptr<Base>> && sizeof(T) == sizeof(Ptr<Base>)) {
        return { Ptr<Ptr<Base>>(begin.offsetAlign4), count };
    }
};

struct Arguments {
    struct Arg {
        Word target;
        Ptr<Expr> source;
    };
    Span<Arg> args;
};

struct WithClause {
    Span<Ptr<LocalDecl>> params;
};

struct Identifier : Arguments {
    Word word;
    bool hasBraces = false;
};

struct Constraint {
    Ptr<Expr> condition;
};

struct Decl {
    DeclKind kind = DeclKind::Invalid;
    Decl(DeclKind kind)
        : kind(kind) { }
};

struct NamedDecl : Decl {
    Word name;
    NamedDecl(DeclKind kind, Word name)
        : Decl(kind), name(name) { }
};

struct StaticDecl : NamedDecl {
    WithClause with;
    Span<Ptr<LocalDecl>> templateParams;
    Span<Ptr<StaticDecl>> staticDecls;
    using NamedDecl::NamedDecl;
};

struct VarInfo {
    Ptr<Expr> type;
    Ptr<Expr> initializer;
    bool isMutable = false;
    bool isInOut = false;
    Span<Constraint> valueConstraints;
    VarInfo(bool isMutable)
        : isMutable(isMutable) { }
};

struct GlobalDecl : StaticDecl, VarInfo {
    GlobalDecl(Word name, bool isMutable)
        : StaticDecl(DeclKind::GlobalDecl, name), VarInfo(isMutable) { }
};

struct LocalDecl : NamedDecl, VarInfo {
    LocalDecl(Word name, bool isMutable)
        : NamedDecl(DeclKind::LocalDecl, name), VarInfo(isMutable) { }
};

struct CallableDecl : StaticDecl {
    Span<Ptr<Decl>> params;
    CallableDecl(DeclKind kind, Word name)
        : StaticDecl(kind, name) { }
};

struct StructDecl : CallableDecl {
    StructDecl(Word name = {})
        : CallableDecl(DeclKind::StructDecl, name) { }
};

struct NamespaceDecl : StaticDecl {
    NamespaceDecl(Word name)
        : StaticDecl(DeclKind::NamespaceDecl, name) { }
};

struct EnumDecl : StaticDecl {
    EnumDecl(Word name)
        : StaticDecl(DeclKind::EnumDecl, name) { }
};
struct EnumValueDecl : StaticDecl {
    Ptr<Expr> enumValue = {};
    EnumValueDecl(Word name)
        : StaticDecl(DeclKind::EnumValueDecl, name) { }
};

struct FnDecl : CallableDecl {
    Ptr<Stmt> body;
    Ptr<LocalDecl> assignParam;
    Ptr<Expr> bodyExpr;
    FnDecl(Word name = {})
        : CallableDecl(DeclKind::FnDecl, name) { }
};

struct HasDecl : Decl, VarInfo {
    Span<Ptr<StaticDecl>> decls;
    HasDecl()
        : Decl(DeclKind::HasDecl), VarInfo(false) { }
};

// Expressions
struct Expr {
    ExprKind kind = ExprKind::Invalid;
    Expr(ExprKind kind)
        : kind(kind) { }
};

enum class UnaryOperator : uint8_t {
    // this must match the order of TokenKind
    // TODO: test this
    LogicalNot, // !
    BitwiseNot, // ~
    PreInc, // ++
    PreDec, // --
    Plus, // +
    Minus, // -

    PostInc,
    PostDec,
    COUNT,
};
constexpr UnaryOperator tokenKindToUnaryOp(TokenKind kind) {
    VERIFY(isUnaryOp(kind));
    return (UnaryOperator)(std::to_underlying(kind) - std::to_underlying(TokenKind::FirstUnaryOp));
}
const char* toShortString(UnaryOperator op);
const char* toOperationString(UnaryOperator op);
struct UnaryOperatorExpr : Expr {
    UnaryOperator op;
    Ptr<Expr> subExpr;
    UnaryOperatorExpr(UnaryOperator op, Ptr<Expr> subExpr = {})
        : Expr(ExprKind::UnaryOperatorExpr), op(op), subExpr(subExpr) { }
};

struct ParenExpr : Expr {
    Arguments args;
    ParenExpr()
        : Expr(ExprKind::ParenExpr) { }
};

struct AccessExpr : Expr {
    bool isStatic = false;
    Ptr<Expr> base;
    Identifier member;
    AccessExpr(bool isStatic, Ptr<Expr> base = {}, Word member = {})
        : Expr(ExprKind::AccessExpr), isStatic(isStatic), base(base), member { {}, member } { }
};

enum class CallKind : uint8_t {
    Paren,
    Angle,
};
struct CallExpr : Expr {
    CallKind callKind;
    Ptr<Expr> base;
    Arguments args;
    CallExpr(CallKind callKind, Ptr<Expr> base = {}, Arguments args = {})
        : Expr(ExprKind::CallExpr), callKind(callKind), base(base), args(args) { }
};

struct IdentifierExpr : Expr {
    Identifier identifier;
    IdentifierExpr(Word identifier = {})
        : Expr(ExprKind::IdentifierExpr), identifier { {}, identifier } { }
};

enum class BinaryOperator : uint8_t {
    // this must match the order binary AND assign operations in TokenKind
    // TODO: test this
    Plus, // +
    Minus, // -
    BitwiseAnd, // &
    LogicalAnd, // &&
    BitwiseXor, // ^
    BitwiseOr, // |
    LogicalOr, // ||
    Multiply, // *
    Divide, // /
    Remainder, // %
    ShiftLeft, // <<
    ShiftRight, // >>
    FirstOp = Plus,
    LastOp = ShiftRight,

    NotEqual, // !=
    Equal, // ==
    Less, // <
    LessEqual, // <=
    Greater, // >
    GreaterEqual, // >=
    FirstCmp = NotEqual,
    LastCmp = GreaterEqual,

    COUNT,
};
constexpr bool isCmpOp(BinaryOperator op) {
    return op >= BinaryOperator::FirstCmp && op <= BinaryOperator::LastCmp;
}
constexpr BinaryOperator tokenKindToBinaryOp(TokenKind kind) {
    VERIFY(isBinaryOp(kind));
    return (BinaryOperator)(std::to_underlying(kind) - std::to_underlying(TokenKind::FirstBinaryOp));
}
const char* toShortString(BinaryOperator op);
const char* toOperationString(BinaryOperator op);
int precedenceOf(BinaryOperator op);
struct BinaryOperatorExpr : Expr {
    BinaryOperator op;
    Ptr<Expr> left;
    Ptr<Expr> right;
    BinaryOperatorExpr(BinaryOperator op, Ptr<Expr> left = {}, Ptr<Expr> right = {})
        : Expr(ExprKind::BinaryOperatorExpr), op(op), left(left), right(right) { }
};

constexpr uint32_t alignmentCeil(uint32_t alignment, uint32_t v) {
    return (v + alignment - 1) & ~(alignment - 1);
}

struct IntLiteralExpr : Expr {
    uint64_t value;
    IntLiteralExpr(uint64_t value)
        : Expr(ExprKind::IntLiteralExpr), value(value) { }
};

struct ConstraintExpr : Expr {
    Constraint constraint;
    ConstraintExpr()
        : Expr(ExprKind::ConstraintExpr) { }
};

struct CompoundExpr : Expr {
    Span<Ptr<Stmt>> body;
    CompoundExpr()
        : Expr(ExprKind::CompoundExpr) { }
};

// Statements
struct Stmt {
    StmtKind kind = StmtKind::Invalid;
    Stmt(StmtKind kind)
        : kind(kind) { }
};

constexpr std::optional<BinaryOperator> tokenKindToAssignOp(TokenKind kind) {
    if (kind == TokenKind::Equal)
        return {};
    VERIFY(isAssignOp(kind));
    return (BinaryOperator)(std::to_underlying(kind) - std::to_underlying(TokenKind::FirstAssignOp));
}
struct AssignStmt : Stmt {
    std::optional<BinaryOperator> op;
    Ptr<Expr> left;
    Ptr<Expr> right;
    AssignStmt(std::optional<BinaryOperator> op, Ptr<Expr> left = {}, Ptr<Expr> right = {})
        : Stmt(StmtKind::AssignStmt), op(op), left(left), right(right) { }
};

struct NullStmt : Stmt {
    NullStmt()
        : Stmt(StmtKind::NullStmt) { }
};

struct CompoundStmt : Stmt {
    Span<Ptr<Stmt>> body;
    CompoundStmt()
        : Stmt(StmtKind::CompoundStmt) { }
};

struct LetStmt : Stmt {
    Ptr<NamedDecl> decl;
    LetStmt(Ptr<NamedDecl> decl = {})
        : Stmt(StmtKind::LetStmt), decl(decl) { }
};

struct ExprStmt : Stmt {
    Ptr<Expr> expr;
    ExprStmt(Ptr<Expr> expr = {})
        : Stmt(StmtKind::ExprStmt), expr(expr) { }
};

struct ReturnStmt : Stmt {
    Ptr<Expr> expr;
    ReturnStmt(Ptr<Expr> expr = {})
        : Stmt(StmtKind::ReturnStmt), expr(expr) { }
};

struct IfStmt : Stmt {
    Ptr<Expr> condition;
    Ptr<Stmt> ifTrue;
    Ptr<Stmt> ifFalse;
    IfStmt(Ptr<Expr> condition = {}, Ptr<Stmt> ifTrue = {}, Ptr<Stmt> ifFalse = {})
        : Stmt(StmtKind::IfStmt), condition(condition), ifTrue(ifTrue), ifFalse(ifFalse) { }
};

struct ForStmt : Stmt {
    Ptr<LocalDecl> loopVarDecl;
    Ptr<Expr> rangeExpr;
    Ptr<Stmt> body;
    ForStmt(Ptr<LocalDecl> loopVarDecl = {}, Ptr<Expr> rangeExpr = {}, Ptr<Stmt> body = {})
        : Stmt(StmtKind::ForStmt), loopVarDecl(loopVarDecl), rangeExpr(rangeExpr), body(body) { }
};

struct WhileStmt : Stmt {
    Ptr<Expr> condition;
    Ptr<Stmt> body;
    WhileStmt(Ptr<Expr> condition = {}, Ptr<Stmt> body = {})
        : Stmt(StmtKind::WhileStmt), condition(condition), body(body) { }
};