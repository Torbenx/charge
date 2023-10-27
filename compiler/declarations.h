#pragma once

#include "StaticDeclProgram.h"
#include "StreamAllocator.h"
#include "WordTable.h"
#include "token.h"
#include <ranges>

struct Node;
struct ParameterOrMemberDecl;
struct Decl;

#define ENUMERATE_DECL_KINDS                    \
    DECL(Namespace, NamespaceDecl)              \
    DECL(StructType, TypeDecl)                  \
    DECL(ObjectType, TypeDecl)                  \
    DECL(Function, FunctionDecl)                \
    DECL(StaticLetVariable, StaticVariableDecl) \
    DECL(StaticMutVariable, StaticVariableDecl) \
    DECL(Member, ParameterOrMemberDecl)         \
    DECL(HasMember, HasMemberDecl)              \
    DECL(LetParameter, ParameterOrMemberDecl)   \
    DECL(MutParameter, ParameterOrMemberDecl)   \
    DECL(InOutParameter, ParameterOrMemberDecl) \
    DECL(OutParameter, ParameterOrMemberDecl)   \
    DECL(BlockLetVariable, BlockVariableDecl)   \
    DECL(BlockMutVariable, BlockVariableDecl)

enum class DeclKind : uint8_t {

#define DECL(kind, type) kind,
    ENUMERATE_DECL_KINDS
#undef DECL

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
template<typename T>
constexpr T* dyn_cast(Decl* in);

struct Decl {
    uint32_t kindBits : 6;
    uint32_t statusBits : 2;
    uint32_t locationBits : 24;
    Word name;
    constexpr Decl(DeclKind kind, WordAndLocation name)
        : kindBits(std::to_underlying(kind))
        , statusBits(std::to_underlying(DeclStatus::Unchecked))
        , locationBits(name.location.tokenStreamOffset)
        , name(name) { VERIFY(isDeclType<Decl>(kind)); }
    DeclKind kind() const { return (DeclKind)kindBits; }
    DeclStatus status() const { return (DeclStatus)statusBits; }
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

    void addDecl(Decl* decl) {
        auto result = findWord(decl->name);
        VERIFY(!result.found);
        entries[result.bucket] = { decl->name, std::bit_cast<uint32_t>(relative_t(this, decl)) };
        usedBuckets += 1;
        maybeRehash();
    }
    named_range decls(Word name) {
        return { named_iterator(this, name) };
    }

    struct iterator {
        using difference_type = int_t;
        using value_type = Decl*;

        StaticDeclContext* context;
        uint32_t bucket;

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
    std::vector<relative_pointer<ParameterDeclContext, ParameterOrMemberDecl>> parameterDecls;
    int_t templateParameterCount = 0;

    void addDecl(ParameterOrMemberDecl* decl) {
        parameterDecls.push_back({ this, decl });
    }

    auto parameters() {
        return std::ranges::views::transform(parameterDecls,
            [this](relative_pointer<ParameterDeclContext, ParameterOrMemberDecl> rel) {
                return rel.get(this);
            });
    }
};

struct ParameterizedDecl : Decl {
    relative_pointer<ParameterizedDecl, ParameterDeclContext> m_parameterDecls;
    StaticDeclProgram program;
    ParameterizedDecl(DeclKind kind, WordAndLocation name, ParameterDeclContext* declContext)
        : Decl(kind, name), m_parameterDecls(this, declContext) { }

    ParameterDeclContext* parameterDecls() { return m_parameterDecls.get(this); }
};
struct NamespaceDecl : Decl {
    StaticDeclContext m_staticDecls;
    NamespaceDecl(WordAndLocation name)
        : Decl(DeclKind::Namespace, name) { }

    StaticDeclContext* staticDecls() { return &m_staticDecls; }
};
struct TypeDecl : ParameterizedDecl {
    StaticDeclContext m_staticDecls;

    TypeDecl(DeclKind kind, WordAndLocation name, ParameterDeclContext* declContext)
        : ParameterizedDecl(kind, name, declContext) { }
    
    StaticDeclContext* staticDecls() { return &m_staticDecls; }
};
struct FunctionDecl : ParameterizedDecl {
    FunctionDecl(WordAndLocation name, ParameterDeclContext* declContext, Node* returnTypeExpr, Node* body)
        : ParameterizedDecl(DeclKind::Function, name, declContext)
        , m_returnTypeExpr(this, returnTypeExpr)
        , m_body(this, body) { }

    relative_pointer<FunctionDecl, Node> m_returnTypeExpr;
    relative_pointer<FunctionDecl, Node> m_body;

    Node* returnTypeExpr() { return m_returnTypeExpr.get(this); }
    Node* body() { return m_body.get(this); }
};
struct StaticVariableDecl : ParameterizedDecl {

    StaticVariableDecl(DeclKind kind, WordAndLocation name, ParameterDeclContext* declContext, Node* typeExpr, Node* initExpr)
        : ParameterizedDecl(kind, name, declContext)
        , m_typeExpr(this, typeExpr)
        , m_initExpr(this, initExpr) {
        VERIFY(isDeclType<StaticVariableDecl>(kind));
    }

    relative_pointer<StaticVariableDecl, Node> m_typeExpr;
    relative_pointer<StaticVariableDecl, Node> m_initExpr;

    Node* typeExpr() { return m_typeExpr.get(this); }
    Node* initExpr() { return m_initExpr.get(this); }

    ConstantStreamInstructionOperand typeValue;
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

template<typename T>
constexpr bool isDeclType(DeclKind kind) {
    switch (kind) {

#define DECL(kind, type) \
    case DeclKind::kind: \
        return std::derived_from<type, T>;
        ENUMERATE_DECL_KINDS
#undef DECL

    default:
        VERIFY_NOT_REACHED();
    }
}

template<typename Target>
constexpr Target* dyn_cast(Decl* source) {
    switch (source->kind()) {

#define DECL(kind, type)                                             \
    case DeclKind::kind: {                                           \
        if constexpr (std::derived_from<type, Target>) {             \
            return static_cast<Target*>(static_cast<type*>(source)); \
        } else {                                                     \
            return nullptr;                                          \
        }                                                            \
    }
        ENUMERATE_DECL_KINDS
#undef DECL

    default:
        VERIFY_NOT_REACHED();
    }
}