#pragma once

#include "StaticDeclProgram.h"
#include "WordTable.h"
#include "token.h"

struct Node;

#define ENUMERATE_DECL_KINDS                        \
    DECL(ModuleDecl, ModuleDecl)                    \
    DECL(NamespaceDecl, NamespaceDecl)              \
    DECL(StructTypeDecl, TypeDecl)                  \
    DECL(ObjectTypeDecl, TypeDecl)                  \
    DECL(FunctionDecl, FunctionDecl)                \
    DECL(StaticLetVariableDecl, StaticVariableDecl) \
    DECL(StaticMutVariableDecl, StaticVariableDecl) \
    DECL(MemberDecl, ParameterOrMemberDecl)         \
    DECL(HasMemberDecl, HasMemberDecl)              \
    DECL(LetParameterDecl, ParameterOrMemberDecl)   \
    DECL(MutParameterDecl, ParameterOrMemberDecl)   \
    DECL(InOutParameterDecl, ParameterOrMemberDecl) \
    DECL(OutParameterDecl, ParameterOrMemberDecl)   \
    DECL(BlockLetDecl, BlockLetDecl)                \
    DECL(BlockMutDecl, BlockLetDecl)

enum class DeclKind : uint8_t {

#define DECL(kind, type) kind,
    ENUMERATE_DECL_KINDS
#undef DECL

};

std::string_view nameString(DeclKind);

template<typename T>
constexpr bool isDeclType(DeclKind);
template<typename T>
constexpr T* dyn_cast(Decl* in);

// declaration arrays
struct DeclArrayItem {
    Word name;
    // negative offset from the beginning of the array group
    relative_pointer<DeclArrayItem, Decl> offset;
};
struct DeclArrayView {
    DeclArrayItem* base = nullptr;
    DeclArrayItem* m_begin = nullptr;
    DeclArrayItem* m_end = nullptr;

    struct iterator {
        using difference_type = int_t;
        using value_type = Decl*;
        DeclArrayItem* base = nullptr;
        DeclArrayItem* item = nullptr;
        constexpr Decl* operator*() const { return (*this)[0]; }
        iterator& operator++() {
            ++item;
            return *this;
        }
        constexpr iterator operator++(int) const { return { base, item + 1 }; }
        constexpr iterator& operator--() {
            --item;
            return *this;
        }
        constexpr iterator operator--(int) const {
            return { base, item - 1 };
        }
        constexpr std::strong_ordering operator<=>(const iterator& other) const { return item <=> other.item; }
        constexpr bool operator==(const iterator& other) const { return item == other.item; }
        constexpr iterator& operator+=(int_t i) {
            item += i;
            return *this;
        }
        constexpr iterator& operator-=(int_t i) {
            item += i;
            return *this;
        }
        constexpr iterator operator+(int_t i) const { return { base, item + i }; }
        constexpr iterator operator-(int_t i) const { return { base, item + i }; }
        constexpr int_t operator-(const iterator& other) const { return item - other.item; }
        constexpr Decl* operator[](int_t i) const { return item[i].offset.get(base); }
    };
    constexpr iterator begin() const {
        return { base, m_begin };
    }
    constexpr iterator end() const {
        return { base, m_end };
    }
    constexpr int_t size() const { return m_end - m_begin; }
    constexpr Decl* operator[](int_t i) const { return begin()[i]; }
};
inline DeclArrayView::iterator operator+(int_t i, const DeclArrayView::iterator& it) { return { it.base, it.item + i }; }
static_assert(std::random_access_iterator<DeclArrayView::iterator>);
struct DeclArrays {
    DeclArrayItem* begin = nullptr;
    uint32_t parameterCount = 0;
    uint32_t staticCount = 0;

    constexpr DeclArrayView view(DeclArrayItem* begin, uint32_t count) const {
        return { this->begin, begin, begin + count };
    }

    constexpr auto all() const {
        return view(begin, parameterCount + staticCount);
    }
    constexpr auto parameters() const {
        return view(begin, parameterCount);
    }
    constexpr auto statics() const {
        return view(begin + parameterCount, staticCount);
    }
};
struct TemplatedDeclArrays : DeclArrays {
    uint32_t withCount = 0;
    uint32_t templateCount = 0;

    constexpr auto withParameters() const {
        return view(begin, withCount);
    }
    constexpr auto templateParamters() const {
        return view(begin + withCount, templateCount);
    }
    constexpr auto callableParameters() const {
        return view(begin + withCount + templateCount, parameterCount - withCount - templateCount);
    }
};

// declarations
struct Decl {
    uint32_t kindBits : 8;
    uint32_t locationBits : 24;
    Word name;
    constexpr Decl(DeclKind kind, WordAndLocation name)
        : kindBits(std::to_underlying(kind))
        , locationBits(name.location.tokenStreamOffset)
        , name(name) { VERIFY(isDeclType<Decl>(kind)); }
    DeclKind kind() const { return (DeclKind)kindBits; }
    SingleTokenSourceRange nameLocation() const { return SingleTokenSourceRange(locationBits); }
};
struct StaticDecl : Decl {
    TemplatedDeclArrays m_decls;

    StaticDeclProgram program;

    constexpr StaticDecl(DeclKind kind, WordAndLocation name, TemplatedDeclArrays decls)
        : Decl(kind, name), m_decls(decls) { VERIFY(isDeclType<StaticDecl>(kind)); }

    TemplatedDeclArrays decls() { return m_decls; }
};
struct TypeDecl : StaticDecl {
    constexpr TypeDecl(DeclKind kind, WordAndLocation name, TemplatedDeclArrays decls)
        : StaticDecl(kind, name, decls) { VERIFY(isDeclType<TypeDecl>(kind)); }
};
struct NamespaceDecl : StaticDecl {
    NamespaceDecl(WordAndLocation name, TemplatedDeclArrays decls)
        : StaticDecl(DeclKind::NamespaceDecl, name, decls) { }
};
struct ModuleDecl : StaticDecl {
    ModuleDecl(TemplatedDeclArrays decls)
        : StaticDecl(DeclKind::ModuleDecl, {}, decls) { }
};
struct FunctionDecl : StaticDecl {

    FunctionDecl(WordAndLocation name, TemplatedDeclArrays decls, Node* returnTypeExpr, Node* body)
        : StaticDecl(DeclKind::FunctionDecl, name, decls)
        , m_returnTypeExpr(this, returnTypeExpr)
        , m_body(this, body) { }

    relative_pointer<FunctionDecl, Node> m_returnTypeExpr;
    relative_pointer<FunctionDecl, Node> m_body;

    Node* returnTypeExpr() { return m_returnTypeExpr.get(this); }
    Node* body() { return m_body.get(this); }
};
struct StaticVariableDecl : StaticDecl {

    StaticVariableDecl(DeclKind kind, WordAndLocation name, TemplatedDeclArrays decls, Node* typeExpr, Node* initExpr)
        : StaticDecl(kind, name, decls)
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
struct HasMemberDecl : ParameterOrMemberDecl {
    DeclArrays m_decls;

    HasMemberDecl(WordAndLocation name, Node* typeExpr, Node* initExpr, DeclArrays decls)
        : ParameterOrMemberDecl(DeclKind::HasMemberDecl, name, typeExpr, initExpr), m_decls(decls) { }

    DeclArrays decls() { return m_decls; }
};
struct BlockLetDecl : Decl {
    relative_pointer<BlockLetDecl, Node> m_typeExpr;
    relative_pointer<BlockLetDecl, Node> m_initExpr;

    BlockLetDecl(DeclKind kind, WordAndLocation name, Node* typeExpr, Node* initExpr)
        : Decl(kind, name)
        , m_typeExpr(this, typeExpr)
        , m_initExpr(this, initExpr) {
        VERIFY(isDeclType<BlockLetDecl>(kind));
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