#pragma once

#include "Lexer.h"

constexpr bool isWordOrGlobal(TokenKind kind) {
    return kind == TokenKind::Word || kind == TokenKind::ColonColon;
}

template<typename E>
struct Ptr {
    uint32_t offsetAlign4 = 0;

    Ptr() = default;
    explicit Ptr(uint32_t off)
        : offsetAlign4(off) { }
    template<typename E2>
    explicit Ptr(const Ptr<E2>& p) requires std::derived_from<E, E2>
        : offsetAlign4(p.offsetAlign4) { }

    template<typename E2>
    operator Ptr<E2>() requires std::derived_from<E, E2> { return Ptr<E2> { offsetAlign4 }; }
};

template<typename T>
struct Span {
    Ptr<T> begin;
    uint32_t count = 0;

    constexpr Ptr<T> operator[](uint32_t i) const requires(sizeof(T) % sizeof(uint32_t) == 0) {
        return Ptr<T> { begin.offsetAlign4 + i * (uint32_t)(sizeof(T) / sizeof(uint32_t)) };
    }
};
template<typename T>
struct SpanBuilder {
    uint32_t begin = 0;
};

struct Word {
    uint32_t start = 0;
    uint32_t length = 0;
};

#define ENUMERATE_STMT_KINDS      \
    STMT_KIND(UnaryOperatorExpr)  \
    STMT_KIND(ParenExpr)          \
    STMT_KIND(AccessExpr)         \
    STMT_KIND(ImmediateBraceExpr) \
    STMT_KIND(CallExpr)           \
    STMT_KIND(IdentifierExpr)     \
    STMT_KIND(BinaryOperatorExpr) \
    STMT_KIND(AssignStmt)         \
    STMT_KIND(NullStmt)           \
    STMT_KIND(CompoundStmt)       \
    STMT_KIND(IntLiteralExpr)     \
    STMT_KIND(LetStmt)

#define STMT_KIND(kind) kind,
enum class StmtKind : uint8_t {
    Invalid,
    ENUMERATE_STMT_KINDS
};
#undef STMT_KIND

struct Stmt {
    StmtKind kind = StmtKind::Invalid;
    Stmt(StmtKind kind)
        : kind(kind) { }
};
struct Expr : Stmt {
    Expr(StmtKind kind)
        : Stmt(kind) { }
};

struct Arguments {
    struct Arg {
        Word target;
        Ptr<Expr> source;
    };
    Span<Arg> args;
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
struct UnaryOperatorExpr : Expr {
    UnaryOperator op;
    Ptr<Expr> subExpr;
    UnaryOperatorExpr(UnaryOperator op, Ptr<Expr> subExpr = {})
        : Expr(StmtKind::UnaryOperatorExpr), op(op), subExpr(subExpr) { }
};

struct ParenExpr : Expr {
    Ptr<Expr> subExpr;
    ParenExpr(Ptr<Expr> subExpr = {})
        : Expr(StmtKind::ParenExpr), subExpr(subExpr) { }
};

struct AccessExpr : Expr {
    Ptr<Expr> base;
    Word member;
    AccessExpr(Ptr<Expr> base = {}, Word member = {})
        : Expr(StmtKind::AccessExpr), base(base), member(member) { }
};

struct ImmediateBraceExpr : Expr {
    Arguments args;
    ImmediateBraceExpr(Arguments args = {})
        : Expr(StmtKind::ImmediateBraceExpr), args(args) { }
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
        : Expr(StmtKind::CallExpr), callKind(callKind), base(base), args(args) { }
};

struct Identifier {
    Span<Word> elements;
    bool global = false;
};
struct ParametricIdentifier : Identifier {
    bool hasBraces = false;
    Arguments args;
};
struct IdentifierExpr : Expr {
    ParametricIdentifier identifier;
    IdentifierExpr(ParametricIdentifier identifier = {})
        : Expr(StmtKind::IdentifierExpr), identifier(identifier) { }
};

enum class BinaryOperator : uint8_t {
    // this must match the order of TokenKind
    // TODO: test this
    Plus, // +
    Minus, // -
    NotEqual, // !=
    Equal, // ==
    BitwiseAnd, // &
    LogicalAnd, // &&
    BitwiseXor, // ^
    BitwiseOr, // |
    LogicalOr, // ||
    Multiply, // *
    Divide, // /
    Remainder, // %
    Less, // <
    ShiftLeft, // <<
    LessEqual, // <=
    Greater, // >
    ShiftRight, // >>
    GreaterEqual, // >=

    COUNT,
};
constexpr BinaryOperator tokenKindToBinaryOp(TokenKind kind) {
    VERIFY(isBinaryOp(kind));
    return (BinaryOperator)(std::to_underlying(kind) - std::to_underlying(TokenKind::FirstBinaryOp));
}
const char* toShortString(BinaryOperator op);
int precedenceOf(BinaryOperator op);
struct BinaryOperatorExpr : Expr {
    BinaryOperator op;
    Ptr<Expr> left;
    Ptr<Expr> right;
    BinaryOperatorExpr(BinaryOperator op, Ptr<Expr> left = {}, Ptr<Expr> right = {})
        : Expr(StmtKind::BinaryOperatorExpr), op(op), left(left), right(right) { }
};

constexpr uint32_t alignmentCeil(uint32_t alignment, uint32_t v) {
    return (v + alignment - 1) & ~(alignment - 1);
}

enum class AssignOperator : uint8_t {
    // this must match the order of TokenKind
    // TODO: test this
    None, // =
    Plus, // +=
    Minus, // -=
    BitwiseAnd, // &=
    BitwiseXor, // ^=
    BitwiseOr, // |=
    Multiply, // *=
    Divide, // /=
    Remainder, // %=
    ShiftLeft, // <<=
    ShiftRight, // >>=

    COUNT,
};
constexpr AssignOperator tokenKindToAssignOp(TokenKind kind) {
    VERIFY(isAssignOp(kind));
    return (AssignOperator)(std::to_underlying(kind) - std::to_underlying(TokenKind::FirstAssignOp));
}
const char* toShortString(AssignOperator op);
struct AssignStmt : Stmt {
    AssignOperator op;
    Ptr<Expr> left;
    Ptr<Expr> right;
    AssignStmt(AssignOperator op, Ptr<Expr> left = {}, Ptr<Expr> right = {})
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

struct IntLiteralExpr : Expr {
    uint64_t value;
    IntLiteralExpr(uint64_t value)
        : Expr(StmtKind::IntLiteralExpr), value(value) { }
};

struct LetStmt : Stmt {
    enum Qualifier : uint8_t {
        None,
        Const,
        Mut,
    };
    bool hasExplicitType = false;
    Qualifier qual;
    Word target;
    ParametricIdentifier typeIdent;
    Ptr<Expr> initializer;
    LetStmt(Qualifier qual, Word target)
        : Stmt(StmtKind::LetStmt), qual(qual), target(target) { }
};

struct STStorage {
    uint32_t* storage = new uint32_t[512] {};

    template<typename E>
    E& at(Ptr<E> e) {
        return *(E*)(storage + e.offsetAlign4);
    }
    template<typename T>
    T& at(Span<T> s, uint32_t i) {
        return *(T*)((std::byte*)&at(s.begin) + i * sizeof(T));
    }

    template<typename E1, typename E2>
    E1& as(Ptr<E2> e) requires std::derived_from<E1, E2> {
        return at(Ptr<E1>(e));
    }
};

struct STContext : STStorage {
    SourceBuffer buffer;

    std::string_view sview(Word word) {
        return { (const char*)(&buffer[word.start]), word.length };
    }
};

const char* toString(StmtKind);

enum class IsLastChild : bool {
    No = false,
    Yes = true,
};

template<typename Impl, typename... Args>
struct STChildren {
    Impl* impl() { return static_cast<Impl*>(this); }
    template<typename T>
    T& Iat(Ptr<T> p) { return impl()->at(p); }

    void childrenUnaryOperatorExpr(Ptr<UnaryOperatorExpr> e, Args... args) {
        impl()->child(Iat(e).subExpr, IsLastChild::Yes, args...);
    }
    void childrenParenExpr(Ptr<ParenExpr> e, Args... args) {
        impl()->child(Iat(e).subExpr, IsLastChild::Yes, args...);
    }
    void childrenAccessExpr(Ptr<AccessExpr> e, Args... args) {
        impl()->child(Iat(e).base, IsLastChild::Yes, args...);
    }
    void childrenImmediateBraceExpr(Ptr<ImmediateBraceExpr> e, Args... args) {
        impl()->child(Iat(e).args, IsLastChild::Yes, args...);
    }
    void childrenCallExpr(Ptr<CallExpr> e, Args... args) {
        if (Iat(e).args.args.count == 0) {
            impl()->child(Iat(e).base, IsLastChild::Yes, args...);
            impl()->child(Iat(e).args, IsLastChild::Yes, args...);
        } else {
            impl()->child(Iat(e).base, IsLastChild::No, args...);
            impl()->child(Iat(e).args, IsLastChild::Yes, args...);
        }
    }
    void childrenIdentifierExpr(Ptr<IdentifierExpr> e, Args... args) {
        impl()->child(Iat(e).identifier.args, IsLastChild::Yes, args...);
    }
    void childrenBinaryOperatorExpr(Ptr<BinaryOperatorExpr> e, Args... args) {
        impl()->child(Iat(e).left, IsLastChild::No, args...);
        impl()->child(Iat(e).right, IsLastChild::Yes, args...);
    }
    void childrenAssignStmt(Ptr<AssignStmt> e, Args... args) {
        impl()->child(Iat(e).left, IsLastChild::No, args...);
        impl()->child(Iat(e).right, IsLastChild::Yes, args...);
    }
    void childrenNullStmt(Ptr<NullStmt>, Args...) { }
    void childrenCompoundStmt(Ptr<CompoundStmt> p, Args... args) {
        CompoundStmt& e = Iat(p);
        for (uint32_t i = 0; i < e.body.count; i++) {
            impl()->child(Iat(e.body[i]), (IsLastChild)(i == e.body.count - 1), args...);
        }
    }
    void childrenIntLiteralExpr(Ptr<IntLiteralExpr>, Args...) { }
    void childrenLetStmt(Ptr<LetStmt> e, Args... args) {
        impl()->child(Iat(e).initializer, IsLastChild::Yes, args...);
    }

    void child(Ptr<Stmt>, IsLastChild, Args...) { }
    void child(Arguments, IsLastChild, Args...) { }

    void dispatchChildren(Ptr<Stmt> e, Args... args) {
#define STMT_KIND(kind)                                \
    case StmtKind::kind:                               \
        impl()->children##kind((Ptr<kind>)e, args...); \
        break;

        switch (Iat(e).kind) {
            ENUMERATE_STMT_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef STMT_KIND
    }
};

template<typename Impl, typename... Args>
struct STVisitor {
    Impl* impl() { return static_cast<Impl*>(this); }
    template<typename T>
    T& Iat(Ptr<T> p) { return impl()->at(p); }

#define STMT_KIND(kind) \
    void visit##kind(Ptr<kind>, Args...) { }
    ENUMERATE_STMT_KINDS
#undef STMT_KIND

    void dispatchVisit(Ptr<Stmt> e, Args... args) {
#define STMT_KIND(kind)                             \
    case StmtKind::kind:                            \
        impl()->visit##kind((Ptr<kind>)e, args...); \
        break;

        switch (Iat(e).kind) {
            ENUMERATE_STMT_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef STMT_KIND
    }
};

struct Parser : Lexer, STStorage {
    using Lexer::Lexer;

    uint32_t storageEndAlign4 = 16;
    uint32_t allocate(uint32_t alignment, uint32_t itemSize, uint32_t itemCount = 1);
    template<typename T>
    Ptr<T> allocate(uint32_t count = 1) {
        return Ptr<T> { allocate(alignof(T), sizeof(T), count) };
    }

    Word asWord(Token token) const {
        return { token.start, token.length };
    }

    std::byte* spanStorage = new std::byte[400] {};
    uint32_t spanBuilderEnd = 0;

    template<typename T>
    SpanBuilder<T> beginSpan() {
        uint32_t oldEnd = spanBuilderEnd;
        spanBuilderEnd = alignmentCeil(alignof(T), oldEnd);
        return { oldEnd };
    }
    template<typename T>
    T& append(SpanBuilder<T>, const T& item) {
        T* ret = new (spanStorage + spanBuilderEnd) T { item };
        spanBuilderEnd += sizeof(T);
        return *ret;
    }
    template<typename T>
    Span<T> finalizeSpan(SpanBuilder<T> s) {
        uint32_t alignedBegin = alignmentCeil(alignof(T), s.begin);
        uint32_t count = (spanBuilderEnd - alignedBegin) / sizeof(T);
        T* beginPtr = (T*)(spanStorage + alignedBegin);

        Ptr<T> outSpan = allocate<T>(count);
        std::uninitialized_move_n(beginPtr, count, &at(outSpan));
        std::destroy_n(beginPtr, count);
        spanBuilderEnd = s.begin;
        return { outSpan, count };
    }

    template<typename E, typename... Args>
    Ptr<E> make(Args&&... args) {
        Ptr<E> p = allocate<E>();
        new (&at(p)) E { std::forward<Args>(args)... };
        return p;
    }
    template<typename E, typename... Args, typename E2>
    E& makeSet(Ptr<E2>& out, Args&&... args) {
        auto p = make<E>(std::forward<Args>(args)...);
        out = p;
        return at(p);
    }

    void parseSimpleIdentifier(Identifier& out);
    void parseParametricIdentifier(ParametricIdentifier& out);
    void parseLeafExpr(Ptr<Expr>& out);
    void wrapWithPostfixes(Ptr<Expr>& out, Ptr<Expr> base);
    void parseBinaryExpr(Ptr<Expr>& out, int precedence = 100);
    void parseBinaryExprAfterFirstExpr(Ptr<Expr>& out, Ptr<Expr> left, int precedence = 100);
    void parseArgumentContext(Arguments& out);
    void parseArgument(Arguments::Arg& out);
    void parseLetStmt(Ptr<Stmt>& out);
    void parseStmt(Ptr<Stmt>& out);
    void parseCompoundStmt(Ptr<CompoundStmt>& out);

    STContext context() {
        return { *this, source };
    }
};

void dump(STContext context, Ptr<Stmt> e);