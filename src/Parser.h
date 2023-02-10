#pragma once

#include "Lexer.h"

constexpr bool isWordOrGlobal(TokenKind kind) {
    return kind == TokenKind::Word;
}

template<typename E>
struct Ptr {
    uint32_t offsetAlign4 = 0;

    Ptr() = default;
    Ptr(uint32_t off)
        : offsetAlign4(off) { }
    template<typename E2>
    explicit Ptr(const Ptr<E2>& p) requires std::derived_from<E, E2>
        : offsetAlign4(p.offsetAlign4) { }

    template<typename E2>
    operator Ptr<E2>() requires std::derived_from<E, E2> { return { offsetAlign4 }; }
};

template<typename T>
struct Span {
    Ptr<T> begin;
    uint32_t count = 0;
};
template<typename T>
struct SpanBuilder {
    uint32_t begin = 0;
};

struct Word {
    uint32_t start = 0;
    uint32_t length = 0;
};

enum class ExprKind : uint32_t {
    Invalid,
    UnaryOperator,
    Paren,
    Access,
    ImmediateBrace,
    Call,
    Identifier,
};

struct Expr {
    ExprKind kind = ExprKind::Invalid;
    uint32_t payload = 0;
    Expr(ExprKind kind, uint32_t payload = 0)
        : kind(kind), payload(payload) { }
};

struct Arguments {
    struct Arg {
        Word target;
        Ptr<Expr> source;
    };
    Span<Arg> args;
};

enum class UnaryOperator : uint32_t {
    // this must match the order of TokenKind
    // TODO: test this
    BitwiseNot,
    PreInc,
    PreDec,
    LogicalNot,
    Plus,
    Minus,

    PostInc,
    PostDec,
    COUNT,
};
constexpr UnaryOperator tokenKindToUnaryOp(TokenKind kind) {
    VERIFY(isUnaryOp(kind));
    return (UnaryOperator)(std::to_underlying(kind) - std::to_underlying(TokenKind::FirstUnaryOp));
}
struct UnaryOperatorExpr : Expr {
    Ptr<Expr> subExpr;
    UnaryOperatorExpr(UnaryOperator op, Ptr<Expr> subExpr = {})
        : Expr(ExprKind::UnaryOperator, std::to_underlying(op))
        , subExpr(subExpr) { }
};

struct ParenExpr : Expr {
    Ptr<Expr> subExpr;
    ParenExpr(Ptr<Expr> subExpr = {})
        : Expr(ExprKind::Paren), subExpr(subExpr) { }
};

struct AccessExpr : Expr {
    Ptr<Expr> subExpr;
    Word member;
    AccessExpr(Ptr<Expr> subExpr = {}, Word member = {})
        : Expr(ExprKind::Access), subExpr(subExpr), member(member) { }
};

struct ImmediateBraceExpr : Expr {
    Arguments args;
    ImmediateBraceExpr(Arguments args = {})
        : Expr(ExprKind::ImmediateBrace), args(args) { }
};

enum class CallKind : uint32_t {
    Paren,
    Angle,
};
struct CallExpr : Expr {
    Ptr<Expr> base;
    Arguments args;
    CallExpr(CallKind callKind, Ptr<Expr> base = {}, Arguments args = {})
        : Expr(ExprKind::Call, std::to_underlying(callKind)), base(base), args(args) { }
};

struct IdentifierExpr : Expr {
    Word identifier;
    IdentifierExpr(Word identifier = {})
        : Expr(ExprKind::Identifier), identifier(identifier) { }
};

constexpr uint32_t alignmentCeil(uint32_t alignment, uint32_t v) {
    return (v + alignment - 1) & -alignment;
}

struct Parser : Lexer {
    using Lexer::Lexer;

    uint32_t* storage = new uint32_t[100] {};
    uint32_t storageEndAlign4 = 0;
    std::byte* spanStorage = new std::byte[400] {};
    uint32_t spanBuilderEnd = 0;

    template<typename E>
    E& at(Ptr<E> e) {
        return *(E*)(storage + e.offsetAlign4);
    }
    template<typename T>
    T& at(Span<T> s, uint32_t i) {
        return (T*)((std::byte*)(storage + s.beginAlign4) + i * sizeof(T));
    }

    uint32_t allocate(uint32_t alignment, uint32_t itemSize, uint32_t itemCount = 1);
    template<typename T>
    Ptr<T> allocate(uint32_t count = 1) {
        return { allocate(alignof(T), sizeof(T), count) };
    }

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
        std::uninitialized_move_n(beginPtr, count, (T*)(storage + outSpan));
        std::destroy_n(beginPtr, count);
        return { outSpan, count };
    }

    template<typename E1, typename E2>
    E1& as(Ptr<E2> e) requires std::derived_from<E1, E2> {
        return at(Ptr<E1>(e));
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

    Word asWord(Token token) const {
        return { token.start, token.length };
    }

    void parseNestedNameOrType(Ptr<Expr>& out);
    void parseLeafExpr(Ptr<Expr>& out);
    void wrapWithPostfixes(Ptr<Expr>& out, Ptr<Expr> base);
    void parseBinaryExpr(Ptr<Expr>& out);
    void parseArgumentContext(Arguments& out);

    void dumpTree(Ptr<Expr>, int = 0);
};