#pragma once

#include "statement.h"
#include <array>
#include <span>

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
    template<typename T>
    std::span<T> at(Span<T> s) {
        return { &at(s.begin), s.count };
    }

    template<typename E1, typename E2>
    E1& as(Ptr<E2> e) requires std::derived_from<E1, E2> {
        return at(Ptr<E1>(e));
    }
};

struct STContext : STStorage {
    SourceBuffer source;

    std::string_view sview(Word word) {
        return { (const char*)(&source[word.start]), word.length };
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
        return { token.start, (uint16_t)token.length };
    }

    std::array<std::byte*, 1> spanStorage = { new std::byte[400] {} };
    std::array<uint32_t, 1> spanBuilderEnd = {};

    template<typename T, int I = 0>
    SpanBuilder<T, I> beginSpan() {
        uint32_t oldEnd = spanBuilderEnd[I];
        spanBuilderEnd[I] = alignmentCeil(alignof(T), oldEnd);
        return { oldEnd };
    }
    template<typename T, int I>
    T& append(SpanBuilder<T, I> s, const T& item) {
        T* ret = new (spanEnd(s)) T { item };
        spanBuilderEnd[I] += sizeof(T);
        return *ret;
    }
    template<typename T, int I>
    Span<T> finalizeSpan(SpanBuilder<T, I> s) {
        uint32_t count = spanSize(s);
        T* beginPtr = spanBegin(s);

        Ptr<T> outSpan = allocate<T>(count);
        std::uninitialized_move_n(beginPtr, count, &at(outSpan));
        std::destroy_n(beginPtr, count);
        spanBuilderEnd[I] = s.begin;
        return { outSpan, count };
    }
    template<typename T, int I>
    T* spanBegin(SpanBuilder<T, I> s) {
        uint32_t alignedBegin = alignmentCeil(alignof(T), s.begin);
        return (T*)(spanStorage[I] + alignedBegin);
    }
    template<typename T, int I>
    T* spanEnd(SpanBuilder<T, I>) {
        return (T*)(spanStorage[I] + spanBuilderEnd[I]);
    }
    template<typename T, int I>
    uint32_t spanSize(SpanBuilder<T, I> s) {
        uint32_t alignedBegin = alignmentCeil(alignof(T), s.begin);
        return (spanBuilderEnd[I] - alignedBegin) / sizeof(T);
    }
    template<typename T, int I>
    T& get(SpanBuilder<T, I> s, uint32_t i) {
        return *(spanBegin(s) + i);
    }
    template<typename T, int I>
    void discardSpan(SpanBuilder<T, I> s) {
        spanBuilderEnd[I] = s.begin;
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

    void parseIdentifier(Ptr<Identifier>& out);
    void parseLeafExpr(Ptr<Expr>& out);
    void wrapWithPostfixes(Ptr<Expr>& out, Ptr<Expr> base);
    void parseBinaryExpr(Ptr<Expr>& out, int precedence = 100);
    void parseArgumentContext(Arguments& out);
    void parseArgument(Arguments::Arg& out);
    void parseLetStmt(Ptr<Stmt>& out);
    void parseStmt(Ptr<Stmt>& out);
    void parseCompoundStmt(Ptr<CompoundStmt>& out);

    void parseParameterContext(Parameters& out);
    void parseParameter(Ptr<VarDecl>& out);
    void parseWithClause(WithClause& out);
    void parseDecl(Ptr<Decl>& out);

    STContext context() {
        return { *this, source };
    }
};

void dump(STContext context, Ptr<Stmt> e);
void dump(STContext context, Ptr<Decl> e);