#pragma once

#include "StreamAllocator.h"
#include "WordTable.h"
#include "token.h"
#include <ranges>

struct Node;
struct Decl;
struct StaticDecl;
struct ParameterizedDecl;

enum class DeclKind : uint8_t {

#define ANY_DECL(kind, type) kind,
#include "declarations.inc"

};

enum class DeclStatus : uint8_t {
    Unchecked,
    SignatureCheckInProgress,
    SignatureChecked,
    FullyChecked,
};

std::string_view nameString(DeclKind);

template<typename T>
constexpr bool isDeclType(DeclKind);

struct Decl {
    uint32_t kindBits : 6;
    uint32_t statusBits : 2;
    uint32_t locationBits : 24;
    Word name;
    constexpr Decl(DeclKind kind, WordAndLocation name)
        : kindBits(std::to_underlying(kind))
        , statusBits(std::to_underlying(
              isDeclType<ParameterizedDecl>(kind)
                  ? DeclStatus::Unchecked
                  : DeclStatus::FullyChecked))
        , locationBits(name.location.tokenStreamOffset)
        , name(name) { VERIFY(isDeclType<Decl>(kind)); }
    DeclKind kind() const { return (DeclKind)kindBits; }
    DeclStatus status() const { return (DeclStatus)statusBits; }
    bool signatureChecked() const { return status() >= DeclStatus::SignatureChecked; }
    void setStatus(DeclStatus status) { statusBits = std::to_underlying(status); }
    SingleTokenSourceRange nameLocation() const { return SingleTokenSourceRange(locationBits); }
};
struct VariableDecl {
    relative_pointer<VariableDecl, Node> m_typeExpr;
    relative_pointer<VariableDecl, Node> m_initExpr;

    VariableDecl(Node* typeExpr, Node* initExpr)
        : m_typeExpr(this, typeExpr)
        , m_initExpr(this, initExpr) { }

    Node* typeExpr() { return m_typeExpr.get(this); }
    Node* initExpr() { return m_initExpr.get(this); }
};
struct ParameterDecl : Decl, VariableDecl {
    ParameterDecl(DeclKind kind, WordAndLocation name, Node* typeExpr, Node* initExpr)
        : Decl(kind, name), VariableDecl(typeExpr, initExpr) {
        VERIFY(isDeclType<ParameterDecl>(kind));
    }
};

struct StaticDeclContext : WordTable {
    using relative_t = relative_pointer<StaticDeclContext, StaticDecl>;

    StaticDecl* getFromBucket(uint32_t bucket) {
        return std::bit_cast<relative_t>(entries[bucket].payload).get(this);
    }

    struct named_iterator_end { };
    struct named_iterator {
        using difference_type = int_t;
        using value_type = StaticDecl*;

        StaticDeclContext* context;
        LookupState state;
        Word name;

        constexpr named_iterator(StaticDeclContext* context, Word name)
            : context(context), name(name) {
            set(context->findWord(name));
        }
        constexpr void set(FindResult result) {
            state = result;
            if (!result.found)
                name = {};
        }
        constexpr value_type operator*() const {
            return context->getFromBucket(state.bucket);
        }
        constexpr named_iterator& operator++() {
            set(context->continueFindWord(name, state));
            return *this;
        }
        constexpr named_iterator operator++(int) const {
            named_iterator out = *this;
            ++out;
            return out;
        }
        constexpr bool operator==(const named_iterator_end&) const {
            return name.empty();
        }
    };
    struct named_range {
        named_iterator m_begin;
        named_iterator begin() const { return m_begin; }
        named_iterator_end end() const { return {}; }

        bool empty() const { return begin() == end(); }
    };

    void addDecl(StaticDecl* decl);
    named_range decls(Word name) {
        return { named_iterator(this, name) };
    }

    struct iterator {
        using difference_type = int_t;
        using value_type = StaticDecl*;

        StaticDeclContext* context;
        int_t bucket;

        iterator& operator++() {
            do
                bucket += 1;
            while (context->entries[bucket].empty() && bucket < context->bucketCount());
            return *this;
        }
        iterator operator++(int) const {
            iterator it = *this;
            ++it;
            return it;
        }
        iterator& operator--() {
            do
                bucket -= 1;
            while (context->entries[bucket].empty() && bucket >= 0);
            return *this;
        }
        iterator operator--(int) const {
            iterator it = *this;
            ++it;
            return it;
        }
        value_type operator*() const { return context->getFromBucket(bucket); }
        bool operator==(const iterator&) const = default;
    };
    iterator begin() { return iterator(this, -1)++; }
    iterator end() { return iterator(this, bucketCount()); }
    static_assert(std::bidirectional_iterator<iterator>);
};

struct ParameterDeclContext {
    struct Entry {
        Entry(ParameterDeclContext* ctx, ParameterDecl* decl)
            : name(decl->name), decl(ctx, decl) { }
        Word name;
        relative_pointer<ParameterDeclContext, ParameterDecl> decl;
    };
    std::vector<Entry> parameterDecls;
    int_t templateParameterCount = 0;

    void addDecl(ParameterDecl* decl) {
        parameterDecls.push_back({ this, decl });
    }

    auto parameters() {
        return std::ranges::views::transform(parameterDecls,
            [this](Entry entry) { return entry.decl.get(this); });
    }
};

struct DeclaringStaticDecl;
struct StaticDecl : Decl {
    relative_pointer<StaticDecl, DeclaringStaticDecl> m_declaringDecl;
    using Decl::Decl;

    void setDeclaringStaticDecl(DeclaringStaticDecl* decl) {
        m_declaringDecl = { this, decl };
    }
    DeclaringStaticDecl* declaringDecl() {
        return m_declaringDecl.get(this);
    }
};
// A static declaration declaring other static declarations.
struct DeclaringStaticDecl : StaticDecl, private StaticDeclContext {
    static constexpr DeclaringStaticDecl* fromContext(StaticDeclContext* ctx) {
        return (DeclaringStaticDecl*)ctx;
    }
    DeclaringStaticDecl(DeclKind kind, WordAndLocation name)
        : StaticDecl(kind, name) { VERIFY(isDeclType<DeclaringStaticDecl>(kind)); }

    StaticDeclContext* staticDecls() { return this; }
};
struct NamespaceDecl : DeclaringStaticDecl {
    NamespaceDecl(WordAndLocation name)
        : DeclaringStaticDecl(DeclKind::Namespace, name) { }
};
struct DeclProgram;
struct ParameterizedDecl {
    relative_pointer<ParameterizedDecl, ParameterDeclContext> m_parameterDecls;
    relative_pointer<ParameterizedDecl, DeclProgram> m_program;
    ParameterizedDecl(ParameterDeclContext* declContext, DeclProgram* program)
        : m_parameterDecls(this, declContext), m_program(this, program) { }

    ParameterDeclContext* parameterDecls() { return m_parameterDecls.get(this); }

    DeclProgram* program() { return m_program.get(this); }
};
struct TypeDecl : DeclaringStaticDecl, ParameterizedDecl {
    TypeDecl(DeclProgram* program, DeclKind kind, WordAndLocation name, ParameterDeclContext* declContext)
        : DeclaringStaticDecl(kind, name), ParameterizedDecl(declContext, program) { }

    static constinit const uint32_t DECL_PROGRAM_SIZE;
    struct Program;
    Program* program();
};
struct FunctionDecl : StaticDecl, ParameterizedDecl {
    FunctionDecl(DeclProgram* program, WordAndLocation name, ParameterDeclContext* declContext, Node* returnTypeExpr, Node* body)
        : StaticDecl(DeclKind::Function, name)
        , ParameterizedDecl(declContext, program)
        , m_returnTypeExpr(this, returnTypeExpr)
        , m_body(this, body) { }

    relative_pointer<FunctionDecl, Node> m_returnTypeExpr;
    relative_pointer<FunctionDecl, Node> m_body;

    Node* returnTypeExpr() { return m_returnTypeExpr.get(this); }
    Node* body() { return m_body.get(this); }

    static constinit const uint32_t DECL_PROGRAM_SIZE;
    struct Program;
    Program* program();
};

struct StaticVariableDecl : StaticDecl, ParameterizedDecl, VariableDecl {
    StaticVariableDecl(DeclProgram* program, DeclKind kind, WordAndLocation name, ParameterDeclContext* declContext, Node* typeExpr, Node* initExpr)
        : StaticDecl(kind, name), ParameterizedDecl(declContext, program), VariableDecl(typeExpr, initExpr) {
        VERIFY(isDeclType<StaticVariableDecl>(kind));
    }

    static constinit const uint32_t DECL_PROGRAM_SIZE;
    struct Program;
    Program* program();
};
struct MemberDecl : Decl, VariableDecl {
    MemberDecl(DeclKind kind, WordAndLocation name, Node* typeExpr, Node* initExpr)
        : Decl(kind, name), VariableDecl(typeExpr, initExpr) { }
};
struct HasMemberDecl : MemberDecl {
    StaticDeclContext m_staticDecls;

    HasMemberDecl(WordAndLocation name, Node* typeExpr, Node* initExpr)
        : MemberDecl(DeclKind::HasMember, name, typeExpr, initExpr) { }

    StaticDeclContext* staticDecls() { return &m_staticDecls; }
};
struct BlockVariableDecl : Decl, VariableDecl {

    BlockVariableDecl(DeclKind kind, WordAndLocation name, Node* typeExpr, Node* initExpr)
        : Decl(kind, name), VariableDecl(typeExpr, initExpr) {
        VERIFY(isDeclType<BlockVariableDecl>(kind));
    }
};
struct BlockDecl : Decl { };

template<typename T>
constexpr bool isDeclType(DeclKind kind) {
    switch (kind) {

#define ANY_DECL(kind, type) \
    case DeclKind::kind:     \
        return std::derived_from<type, T>;
#include "declarations.inc"

    default:
        VERIFY_NOT_REACHED();
    }
}

template<typename Target>
constexpr std::optional<Target*> dyn_cast(Decl* source) {
    switch (source->kind()) {

#define ANY_DECL(kind, type)                                         \
    case DeclKind::kind: {                                           \
        if constexpr (std::derived_from<type, Target>) {             \
            return static_cast<Target*>(static_cast<type*>(source)); \
        } else {                                                     \
            return std::nullopt;                                     \
        }                                                            \
    }
#include "declarations.inc"

    default:
        VERIFY_NOT_REACHED();
    }
}