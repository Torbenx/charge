#pragma once

#include "statement.h"
#include <array>
#include <span>
#include <vector>

inline constexpr uint32_t BUMP_START_OFFSET_ALIGN4 = 128;

struct STStorage {
    uint32_t* storage = new uint32_t[4096] {};

    template<typename E>
    E& at(Ptr<E> e) {
        VERIFY(e.offsetAlign4 >= BUMP_START_OFFSET_ALIGN4);
        return *(E*)(storage + e.offsetAlign4);
    }
    template<typename T>
    T& at(Span<T> s, uint32_t i) {
        return *(&at(s.begin) + i);
    }
    template<typename T>
    std::span<T> at(Span<T> s) {
        VERIFY(s.count == 0 || s.begin.offsetAlign4 >= BUMP_START_OFFSET_ALIGN4);
        return { (T*)(storage + s.begin.offsetAlign4), s.count };
    }

    template<typename E1, typename E2>
    E1& as(Ptr<E2> e) requires std::derived_from<E1, E2> {
        return at(Ptr<E1>(e));
    }

    bool isStaticDecl(Ptr<Decl> decl) {
        switch (at(decl).kind) {
        case DeclKind::FnDecl:
        case DeclKind::GlobalDecl:
        case DeclKind::StructDecl:
            return true;
        default:
            return false;
        }
    }

    Ptr<StaticDecl> asStaticDecl(Ptr<Decl> decl) {
        if (decl && isStaticDecl(decl))
            return (Ptr<StaticDecl>)decl;
        return {};
    }
    Ptr<VarInfo> asVar(Ptr<Decl> decl) {
        if (!decl)
            return {};
        switch (at(decl).kind) {
        case DeclKind::LocalDecl:
            return (Ptr<LocalDecl>)decl;
        case DeclKind::GlobalDecl:
            return (Ptr<GlobalDecl>)decl;
        default:
            return {};
        }
    }
    Ptr<FnInfo> asFn(Ptr<Decl> decl) {
        if (!decl)
            return {};
        switch (at(decl).kind) {
        case DeclKind::FnDecl:
            return (Ptr<FnDecl>)decl;
        case DeclKind::MethodDecl:
            return (Ptr<MethodDecl>)decl;
        default:
            return {};
        }
    }
};

struct STContext : STStorage {
    std::span<SourceBuffer> sources;

    std::string_view sview(Word word) {
        return { (const char*)&sources[word.bufferId][word.start], word.length };
    }
};

struct Parser : Lexer, STStorage {
    Parser() = default;
    Parser(SourceBuffer buffer, bool dump = false) {
        dumpTokens = dump;
        setSourceBuffer(buffer);
    }

    uint32_t sourceId = 0;
    std::vector<SourceBuffer> sourceBuffers;
    void setSourceBuffer(SourceBuffer buffer) {
        sourceId = sourceBuffers.size();
        sourceBuffers.push_back(buffer);
        Lexer::reset(buffer);
    }

    template<typename T, int I>
    struct SpanBuilder {
        uint32_t begin = 0;
    };

    uint32_t storageEndAlign4 = BUMP_START_OFFSET_ALIGN4;
    uint32_t allocate(uint32_t alignment, uint32_t itemSize, uint32_t itemCount = 1);
    template<typename T>
    Ptr<T> allocate(uint32_t count = 1) {
        return Ptr<T> { allocate(alignof(T), sizeof(T), count) };
    }

    Word asWord(Token token) const {
        return { token.start, (uint16_t)token.length, (uint16_t)sourceId };
    }

    std::array<std::byte*, 2> spanStorage = { new std::byte[400] {}, new std::byte[400] {} };
    std::array<uint32_t, 2> spanBuilderEnd = {};

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

    void parseLeafExpr(Ptr<Expr>& out);
    void wrapWithPostfixes(Ptr<Expr>& out, Ptr<Expr> base);
    void parseBinaryExpr(Ptr<Expr>& out, int precedence = 100);
    void parseArgumentContext(Arguments& out);
    void parseArgument(Arguments::Arg& out);
    void parseLetStmt(Ptr<Stmt>& out);
    void parseReturnStmt(Ptr<Stmt>& out);
    void parseIfStmt(Ptr<Stmt>& out);
    void parseExprOrAssignStmt(Ptr<Stmt>& out);
    void parseStmt(Ptr<Stmt>& out);
    void parseCompoundStmt(Ptr<CompoundStmt>& out);
    void parseSingleOrCompoundStmt(Ptr<Stmt>& out);

    void parseParameterContext(Parameters& out);
    void parseParameter(Ptr<LocalDecl>& out);
    void parseWithClause(WithClause& out);
    enum DeclParseScope {
        Namespace,
        Struct,
    };
    void parseDecl(Ptr<Decl>& out, DeclParseScope scope);

    STContext context() {
        return { *this, sourceBuffers };
    }
};

void dump(STContext context, Ptr<Stmt> e, std::string_view name = {});
void dump(STContext context, Ptr<Expr> e, std::string_view name = {});
void dump(STContext context, Ptr<Decl> e, std::string_view name = {});