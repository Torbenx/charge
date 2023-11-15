#pragma once

#include "StaticDeclProgram.h"
#include "StreamAllocator.h"
#include "WordTable.h"
#include "token.h"
#include <ranges>

struct Node;
struct ParameterOrMemberDecl;
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
// a parameter or (has-)member declaration
struct ParameterOrMemberDecl : Decl {
    relative_pointer<ParameterOrMemberDecl, Node> m_typeExpr;
    relative_pointer<ParameterOrMemberDecl, Node> m_initExpr;

    ParameterOrMemberDecl(DeclKind kind, WordAndLocation name, Node* typeExpr, Node* initExpr)
        : Decl(kind, name)
        , m_typeExpr(this, typeExpr)
        , m_initExpr(this, initExpr) {
        VERIFY(isDeclType<ParameterOrMemberDecl>(kind));
    }

    Node* typeExpr() { return m_typeExpr.get(this); }
    Node* initExpr() { return m_initExpr.get(this); }
};

struct StaticDeclContext : WordTable {
    using relative_t = relative_pointer<StaticDeclContext, Decl>;

    Decl* getFromBucket(uint32_t bucket) {
        return std::bit_cast<relative_t>(entries[bucket].payload).get(this);
    }

    struct named_iterator_end { };
    struct named_iterator {
        using difference_type = int_t;
        using value_type = Decl*;

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
        using value_type = Decl*;

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
        Decl* operator*() const { return context->getFromBucket(bucket); }
        bool operator==(const iterator&) const = default;
    };
    iterator begin() { return iterator(this, -1)++; }
    iterator end() { return iterator(this, bucketCount()); }
    static_assert(std::bidirectional_iterator<iterator>);
};

struct ParameterDeclContext {
    struct Entry {
        Entry(ParameterDeclContext* ctx, ParameterOrMemberDecl* decl)
            : name(decl->name), decl(ctx, decl) { }
        Word name;
        relative_pointer<ParameterDeclContext, ParameterOrMemberDecl> decl;
    };
    std::vector<Entry> parameterDecls;
    int_t templateParameterCount = 0;

    void addDecl(ParameterOrMemberDecl* decl) {
        parameterDecls.push_back({ this, decl });
    }

    Decl* find(Word name) {
        for (Entry entry : parameterDecls) {
            if (entry.name == name)
                return entry.decl.get(this);
        }
        return nullptr;
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
enum class ParameterModel : uint8_t {
    Template,
    ImplicitTemplate,
    In,
    Var,
    InOut,
    Out,
};
constexpr ParameterModel kindToModel(DeclKind kind) {
    switch (kind) {
    case DeclKind::InParameter:
        return ParameterModel::In;
    case DeclKind::VarParameter:
        return ParameterModel::Var;
    case DeclKind::InOutParameter:
        return ParameterModel::InOut;
    case DeclKind::OutParameter:
        return ParameterModel::Out;
    default:
        VERIFY_NOT_REACHED();
    }
}
struct ParameterizedDecl {
    relative_pointer<ParameterizedDecl, ParameterDeclContext> m_parameterDecls;
    StaticDeclProgram program;
    ParameterizedDecl(ParameterDeclContext* declContext)
        : m_parameterDecls(this, declContext) { }

    ParameterDeclContext* parameterDecls() { return m_parameterDecls.get(this); }

    struct CheckedParameter {
        Word name;
        ParameterModel model;
        ConstantStreamInstructionOperand type;
    };
    std::vector<CheckedParameter> parameters;
};
struct NamespaceDecl : DeclaringStaticDecl {
    NamespaceDecl(WordAndLocation name)
        : DeclaringStaticDecl(DeclKind::Namespace, name) { }
};
struct TypeDecl : DeclaringStaticDecl, ParameterizedDecl {
    TypeDecl(DeclKind kind, WordAndLocation name, ParameterDeclContext* declContext)
        : DeclaringStaticDecl(kind, name), ParameterizedDecl(declContext) { }
};
struct FunctionDecl : StaticDecl, ParameterizedDecl {
    FunctionDecl(WordAndLocation name, ParameterDeclContext* declContext, Node* returnTypeExpr, Node* body)
        : StaticDecl(DeclKind::Function, name)
        , ParameterizedDecl(declContext)
        , m_returnTypeExpr(this, returnTypeExpr)
        , m_body(this, body) { }

    relative_pointer<FunctionDecl, Node> m_returnTypeExpr;
    relative_pointer<FunctionDecl, Node> m_body;

    Node* returnTypeExpr() { return m_returnTypeExpr.get(this); }
    Node* body() { return m_body.get(this); }

    std::optional<ConstantStreamInstructionOperand> returnType;
};

struct StaticVariableDecl : StaticDecl, ParameterizedDecl {

    StaticVariableDecl(DeclKind kind, WordAndLocation name, ParameterDeclContext* declContext, Node* typeExpr, Node* initExpr)
        : StaticDecl(kind, name)
        , ParameterizedDecl(declContext)
        , m_typeExpr(this, typeExpr)
        , m_initExpr(this, initExpr) { VERIFY(isDeclType<StaticVariableDecl>(kind)); }

    relative_pointer<StaticVariableDecl, Node> m_typeExpr;
    relative_pointer<StaticVariableDecl, Node> m_initExpr;

    Node* typeExpr() { return m_typeExpr.get(this); }
    Node* initExpr() { return m_initExpr.get(this); }

    ConstantStreamInstructionOperand typeValue;
    ConstantStreamInstructionOperand initValue;
};
struct HasMemberDecl : ParameterOrMemberDecl {
    StaticDeclContext m_staticDecls;

    HasMemberDecl(WordAndLocation name, Node* typeExpr, Node* initExpr)
        : ParameterOrMemberDecl(DeclKind::HasMember, name, typeExpr, initExpr) { }

    StaticDeclContext* staticDecls() { return &m_staticDecls; }
};
struct BlockVariableDecl : Decl {
    relative_pointer<BlockVariableDecl, Node> m_typeExpr;
    relative_pointer<BlockVariableDecl, Node> m_initExpr;

    BlockVariableDecl(DeclKind kind, WordAndLocation name, Node* typeExpr, Node* initExpr)
        : Decl(kind, name), m_typeExpr(this, typeExpr), m_initExpr(this, initExpr) {
        VERIFY(isDeclType<BlockVariableDecl>(kind));
    }

    Node* typeExpr() { return m_typeExpr.get(this); }
    Node* initExpr() { return m_initExpr.get(this); }
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