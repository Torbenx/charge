#pragma once

#include "statement.h"
#include <array>

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

struct Parser : Lexer, STStorage {
    using Lexer::Lexer;

    template<typename T, int I>
    struct SpanBuilder {
        uint32_t begin = 0;
    };

    uint32_t storageEndAlign4 = 16;
    uint32_t allocate(uint32_t alignment, uint32_t itemSize, uint32_t itemCount = 1);
    template<typename T>
    Ptr<T> allocate(uint32_t count = 1) {
        return Ptr<T> { allocate(alignof(T), sizeof(T), count) };
    }

    Word asWord(Token token) const {
        return { token.start, token.length };
    }

    std::array<std::byte*, 2> spanStorage = { new std::byte[400] {}, new std::byte[400] {} };
    std::array<uint32_t, 2> spanBuilderEnd = {};

    template<typename T, int I = 0>
    SpanBuilder<T, I> beginSpan() {
        uint32_t oldEnd = spanBuilderEnd[I];
        spanBuilderEnd[I] = alignmentCeil(alignof(T), oldEnd);
        return { oldEnd };
    }
    SpanBuilder<Ptr<Decl>, 1> beginDeclSpan() {
        return beginSpan<Ptr<Decl>, 1>();
    }
    template<typename T, int I>
    T& append(SpanBuilder<T, I>, const T& item) {
        T* ret = new (spanStorage[I] + spanBuilderEnd[I]) T { item };
        spanBuilderEnd[I] += sizeof(T);
        return *ret;
    }
    template<typename T, int I>
    Span<T> finalizeSpan(SpanBuilder<T, I> s) {
        uint32_t alignedBegin = alignmentCeil(alignof(T), s.begin);
        uint32_t count = (spanBuilderEnd[I] - alignedBegin) / sizeof(T);
        T* beginPtr = (T*)(spanStorage[I] + alignedBegin);

        Ptr<T> outSpan = allocate<T>(count);
        std::uninitialized_move_n(beginPtr, count, &at(outSpan));
        std::destroy_n(beginPtr, count);
        spanBuilderEnd[I] = s.begin;
        return { outSpan, count };
    }
    Ptr<Decl>& emitDecl(Ptr<Decl> decl) {
        return append<Ptr<Decl>, 1>({}, decl);
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
    void parseArgumentContext(Arguments& out);
    void parseArgument(Arguments::Arg& out);
    void parseLetStmt(Ptr<Stmt>& out);
    void parseStmt(Ptr<Stmt>& out);
    void parseCompoundStmt(Ptr<CompoundStmt>& out);

    void parseStructDecl(Ptr<StructDecl>& out);

    STContext context() {
        return { *this, source };
    }
};

void dump(STContext context, Ptr<Stmt> e);