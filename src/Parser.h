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
        return { begin.offsetAlign4 + i * sizeof(T) / sizeof(uint32_t) };
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

// clang-format off
#define ENUMERATE_NODE_KINDS(callback) \
    callback(UnaryOperatorExpr) \
    callback(ParenExpr) \
    callback(AccessExpr) \
    callback(ImmediateBraceExpr) \
    callback(CallExpr) \
    callback(IdentifierExpr) \
    callback(BinaryOperatorExpr) \
    callback(AssignStmt)
// clang-format on

#define NODE_KIND(kind) kind,
enum class NodeKind : uint8_t {
    Invalid,
    ENUMERATE_NODE_KINDS(NODE_KIND)
};
#undef NODE_KIND

struct Node {
    NodeKind kind = NodeKind::Invalid;
    Node(NodeKind kind)
        : kind(kind) { }
};
struct Stmt : Node {
    Stmt(NodeKind kind)
        : Node(kind) { }
};
struct Expr : Stmt {
    Expr(NodeKind kind)
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
        : Expr(NodeKind::UnaryOperatorExpr), op(op), subExpr(subExpr) { }
};

struct ParenExpr : Expr {
    Ptr<Expr> subExpr;
    ParenExpr(Ptr<Expr> subExpr = {})
        : Expr(NodeKind::ParenExpr), subExpr(subExpr) { }
};

struct AccessExpr : Expr {
    Ptr<Expr> base;
    Word member;
    AccessExpr(Ptr<Expr> base = {}, Word member = {})
        : Expr(NodeKind::AccessExpr), base(base), member(member) { }
};

struct ImmediateBraceExpr : Expr {
    Arguments args;
    ImmediateBraceExpr(Arguments args = {})
        : Expr(NodeKind::ImmediateBraceExpr), args(args) { }
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
        : Expr(NodeKind::CallExpr), callKind(callKind), base(base), args(args) { }
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
        : Expr(NodeKind::IdentifierExpr), identifier(identifier) { }
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
        : Expr(NodeKind::BinaryOperatorExpr), op(op), left(left), right(right) { }
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
        : Stmt(NodeKind::AssignStmt), op(op), left(left), right(right) { }
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

const char* toString(NodeKind);

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

    void child(Ptr<Node>, IsLastChild, Args...) { }
    void child(Arguments, IsLastChild, Args...) { }

    void dispatchChildren(Ptr<Node> e, Args... args) {
#define NODE_KIND(kind)                                \
    case NodeKind::kind:                               \
        impl()->children##kind((Ptr<kind>)e, args...); \
        break;

        switch (Iat(e).kind) {
            ENUMERATE_NODE_KINDS(NODE_KIND)
        default:
            VERIFY_NOT_REACHED();
        }

#undef NODE_KIND
    }
};

template<typename Impl, typename... Args>
struct STVisitor {
    Impl* impl() { return static_cast<Impl*>(this); }
    template<typename T>
    T& Iat(Ptr<T> p) { return impl()->at(p); }

#define NODE_KIND(kind) \
    void visit##kind(Ptr<kind>, Args...) { }
    ENUMERATE_NODE_KINDS(NODE_KIND)
#undef NODE_KIND

    void dispatchVisit(Ptr<Node> e, Args... args) {
#define NODE_KIND(kind)                             \
    case NodeKind::kind:                            \
        impl()->visit##kind((Ptr<kind>)e, args...); \
        break;

        switch (Iat(e).kind) {
            ENUMERATE_NODE_KINDS(NODE_KIND)
        default:
            VERIFY_NOT_REACHED();
        }

#undef NODE_KIND
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
    void parseStmt(Ptr<Stmt>& out);

    STContext context() {
        return { *this, buffer };
    }
};

void dump(STContext context, Ptr<Node> e);