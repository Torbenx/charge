#pragma once

#include "statement.h"
#include <array>
#include <span>
#include <unordered_map>
#include <vector>

inline constexpr uint32_t BUMP_START_OFFSET_ALIGN4 = 128;

struct STStorage {
    static constexpr uint32_t storageArraySizeAlign4 = 16384;
    uint32_t* stStorage = new uint32_t[storageArraySizeAlign4] {};
    uint32_t storageEndAlign4 = BUMP_START_OFFSET_ALIGN4;

    std::unordered_map<std::string, uint32_t> wordMap = {};
    uint32_t nextWordId = 1;

    std::array<std::byte*, 2> spanStorage = { new std::byte[400] {}, new std::byte[400] {} };
    std::array<uint32_t, 2> spanBuilderEnd = {};

    uint32_t allocate(uint32_t alignment, uint32_t itemSize, uint32_t itemCount = 1);
};

class STContext {
public:
    std::shared_ptr<STStorage> storage;

    static STContext create() {
        STContext ctx { std::make_shared<STStorage>() };
        return ctx;
    }

    Word asWord(std::string_view view);
    static constexpr uint32_t CONVERSION_WORD_ID = -1;
    Word structWord = asWord("struct");
    Word fnWord = asWord("fn");
    Word letWord = asWord("let");
    Word mutWord = asWord("mut");
    Word staticWord = asWord("static");
    Word ifWord = asWord("if");
    Word elseWord = asWord("else");
    Word elifWord = asWord("elif");
    Word forWord = asWord("for");
    Word inWord = asWord("in");
    Word whileWord = asWord("while");
    Word returnWord = asWord("return");
    Word withWord = asWord("with");
    Word templateWord = asWord("template");
    Word hasWord = asWord("has");
    Word enumWord = asWord("enum");
    Word namespaceWord = asWord("namespace");

    std::string_view sview(Word word) const;

    template<typename E>
    E& at(Ptr<E> e) {
        VERIFY(e.offsetAlign4 >= BUMP_START_OFFSET_ALIGN4);
        return *(E*)(storage->stStorage + e.offsetAlign4);
    }
    template<typename T>
    T& at(Span<T> s, uint32_t i) {
        return *(&at(s.begin) + i);
    }
    template<typename T>
    std::span<T> at(Span<T> s) {
        VERIFY(s.count == 0 || s.begin.offsetAlign4 >= BUMP_START_OFFSET_ALIGN4);
        return { (T*)(storage->stStorage + s.begin.offsetAlign4), s.count };
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
        case DeclKind::NamespaceDecl:
        case DeclKind::EnumDecl:
        case DeclKind::EnumValueDecl:
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
        case DeclKind::HasDecl:
            return (Ptr<HasDecl>)decl;
        default:
            return {};
        }
    }
    bool isNamedDecl(Ptr<Decl> decl) {
        return at(decl).kind != DeclKind::HasDecl;
    }
    Ptr<NamedDecl> asNamedDecl(Ptr<Decl> decl) {
        if (decl && isNamedDecl(decl))
            return (Ptr<NamedDecl>)decl;
        return {};
    }

    bool isPrimaryDecl(Ptr<Decl> decl) {
        Decl& d = at(decl);
        switch (d.kind) {
        case DeclKind::FnDecl:
            return !((FnDecl&)d).assignParam;
        default:
            return true;
        }
    }

    template<typename T>
    Ptr<T> allocate(uint32_t count = 1) {
        return Ptr<T> { storage->allocate(alignof(T), sizeof(T), count) };
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

    template<typename T, int I>
    struct SpanBuilder {
        uint32_t begin = 0;
    };

    template<typename T, int I = 0>
    SpanBuilder<T, I> beginSpan() {
        uint32_t oldEnd = storage->spanBuilderEnd[I];
        storage->spanBuilderEnd[I] = alignmentCeil(alignof(T), oldEnd);
        return { oldEnd };
    }
    template<typename T, int I>
    T& append(SpanBuilder<T, I> s, const std::type_identity_t<T>& item) {
        T* ret = new (spanEnd(s)) T { item };
        storage->spanBuilderEnd[I] += sizeof(T);
        return *ret;
    }
    template<typename T, int I>
    Span<T> finalizeSpan(SpanBuilder<T, I> s) {
        uint32_t count = spanSize(s);
        T* beginPtr = spanBegin(s);

        Ptr<T> outSpan = allocate<T>(count);
        std::uninitialized_move_n(beginPtr, count, &at(outSpan));
        std::destroy_n(beginPtr, count);
        storage->spanBuilderEnd[I] = s.begin;
        return { outSpan, count };
    }
    template<typename T, int I>
    T* spanBegin(SpanBuilder<T, I> s) {
        uint32_t alignedBegin = alignmentCeil(alignof(T), s.begin);
        return (T*)(storage->spanStorage[I] + alignedBegin);
    }
    template<typename T, int I>
    T* spanEnd(SpanBuilder<T, I>) {
        return (T*)(storage->spanStorage[I] + storage->spanBuilderEnd[I]);
    }
    template<typename T, int I>
    uint32_t spanSize(SpanBuilder<T, I> s) {
        uint32_t alignedBegin = alignmentCeil(alignof(T), s.begin);
        return (storage->spanBuilderEnd[I] - alignedBegin) / sizeof(T);
    }
    template<typename T, int I>
    T& get(SpanBuilder<T, I> s, uint32_t i) {
        return *(spanBegin(s) + i);
    }
    template<typename T, int I>
    void discardSpan(SpanBuilder<T, I> s) {
        storage->spanBuilderEnd[I] = s.begin;
    }
};

struct Parser : STContext {
    SourceBuffer source;
    uint32_t m_position = 0;
    uint32_t pos() const { return m_position; }
    bool dumpTokens = false;

    void reset(SourceBuffer);
    void advance();
    uint32_t nextNonWhiteSpace(uint32_t pos) const;
    void skipWhiteSpace() { m_position = nextNonWhiteSpace(pos()); }

    Token tok = {};

    Parser(STContext context, SourceBuffer buffer)
        : STContext(std::move(context)) { reset(buffer); }

    uint64_t lexInteger();
    void parseLeafExpr(Ptr<Expr>& out);
    void wrapWithPostfixes(Ptr<Expr>& out, Ptr<Expr> base);
    void parseBinaryExpr(Ptr<Expr>& out, int precedence = 100);
    void parseIfExpr(Ptr<Expr>& out);
    void parseExpr(Ptr<Expr>& out);
    void parseArgumentContext(Arguments& out);
    void parseArgument(Arguments::Arg& out);
    void parseLetStmt(Ptr<Stmt>& out);
    void parseReturnStmt(Ptr<Stmt>& out);
    void parseIfBranch(Ptr<Stmt>& out);
    void parseIfStmt(Ptr<Stmt>& out);
    void parseForStmt(Ptr<Stmt>& out);
    void parseWhileStmt(Ptr<Stmt>& out);
    void parseExprOrAssignStmt(Ptr<Stmt>& out);
    void parseStmt(Ptr<Stmt>& out);
    Span<Ptr<Stmt>> parseStmts();
    void parseCompoundStmt(Ptr<CompoundStmt>& out);
    void parseSingleOrCompoundStmt(Ptr<Stmt>& out);
    enum ParameterParseScope {
        Static, // basic parameters, ex: 'with' and 'template'
        Function, // allow 'mut' and '&'
    };
    Span<Ptr<LocalDecl>> parseParameterContext(ParameterParseScope);
    void parseParameter(Ptr<LocalDecl>& out, ParameterParseScope);
    void parseWithClause(WithClause& out);
    enum DeclParseScope {
        Namespace,
        Struct,
        Has,
    };
    void parseFunctionDefinition(FnDecl& out);
    void parseDecl(Ptr<Decl>& out, DeclParseScope scope);
};

void dump(STContext context, Ptr<Stmt> e, std::string_view name = {});
void dump(STContext context, Ptr<Expr> e, std::string_view name = {});
void dump(STContext context, Ptr<Decl> e, std::string_view name = {});