#pragma once

#include "StreamAllocator.h"
#include "WordTable.h"
#include "token.h"
#include <utility>

using node_stream_offset = aligned_t<4>;

enum class NodeKind : uint8_t {

#define NODE(kind, type) kind,
#include "nodes.h"

    COUNT,
};

template<typename T>
constexpr bool matchNodeType(NodeKind in);
template<typename T>
constexpr bool isNodeType(NodeKind in);

std::string_view nameString(NodeKind);

struct DeclArrayItem;

struct Node {
    static constexpr int KIND_BITS = 8;
    static_assert(std::to_underlying(NodeKind::COUNT) < 1 << KIND_BITS);
    uint32_t kindBits : KIND_BITS;
    uint32_t streamOffsetBits : (32 - KIND_BITS);

    Node(NodeKind kind, SingleTokenSourceRange token)
        : kindBits(std::to_underlying(kind)), streamOffsetBits(token.tokenStreamOffset) { }
    SingleTokenSourceRange packedToken() const {
        return SingleTokenSourceRange(streamOffsetBits);
    }
    NodeKind kind() const { return NodeKind(kindBits); }

    using backwards_offset = node_stream_offset;
    backwards_offset backwardsOffsetTo(Node* target) const {
        return backwards_offset::backwords_diff(target, this);
    }
    backwards_offset backwardsOffsetTo(DeclArrayItem* target) const {
        return backwards_offset::backwords_diff(target, this);
    }
    Node* followBackwardsOffset(backwards_offset offset) {
        return (Node*)((std::byte*)this - offset);
    }
    DeclArrayItem* followBackwardsOffsetToArray(backwards_offset offset) {
        return (DeclArrayItem*)((std::byte*)this - offset);
    }
};
struct EndScope : Node {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
    EndScope(SingleTokenSourceRange endLoc)
        : Node(NodeKind::EndScope, endLoc) { }
    EndScope()
        : Node(NodeKind::EndScope, SingleTokenSourceRange(0)) { }
    SingleTokenSourceRange scopeEndLocation() const { return packedToken(); }
};

// declaration arrays
struct Decl;
struct DeclArrayItem {
    Word name;
    // negative offset from the beginning of the array group
    node_stream_offset offset;
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
        Decl* operator*() const { return (*this)[0]; }
        iterator& operator++() {
            ++item;
            return *this;
        }
        iterator operator++(int) const { return { base, item + 1 }; }
        iterator& operator--() {
            --item;
            return *this;
        }
        iterator operator--(int) const {
            return { base, item - 1 };
        }
        std::strong_ordering operator<=>(const iterator& other) const { return item <=> other.item; }
        bool operator==(const iterator& other) const { return item == other.item; }
        iterator& operator+=(int_t i) {
            item += i;
            return *this;
        }
        iterator& operator-=(int_t i) {
            item += i;
            return *this;
        }
        iterator operator+(int_t i) const { return { base, item + i }; }
        iterator operator-(int_t i) const { return { base, item + i }; }
        int_t operator-(const iterator& other) const { return item - other.item; }
        Decl* operator[](int_t i) const { return (Decl*)((std::byte*)base - item[i].offset); }
    };
    iterator begin() const {
        return { base, m_begin };
    }
    iterator end() const {
        return { base, m_end };
    }
    int_t size() const { return m_end - m_begin; }
    Decl* operator[](int_t i) const { return begin()[i]; }
};
inline DeclArrayView::iterator operator+(int_t i, const DeclArrayView::iterator& it) { return { it.base, it.item + i }; }
static_assert(std::random_access_iterator<DeclArrayView::iterator>);
struct DeclArrays {
    DeclArrayItem* begin = nullptr;
    uint32_t parameterCount = 0;
    uint32_t staticCount = 0;

    DeclArrayView view(DeclArrayItem* begin, uint32_t count) const {
        return { this->begin, begin, begin + count };
    }

    auto all() const {
        return view(begin, parameterCount + staticCount);
    }
    auto parameters() const {
        return view(begin, parameterCount);
    }
    auto statics() const {
        return view(begin + parameterCount, staticCount);
    }
};
struct TemplatedDeclArrays : DeclArrays {
    uint32_t withCount = 0;
    uint32_t templateCount = 0;

    auto withParameters() const {
        return view(begin, withCount);
    }
    auto templateParamters() const {
        return view(begin + withCount, templateCount);
    }
    auto callableParameters() const {
        return view(begin + withCount + templateCount, parameterCount - withCount - templateCount);
    }
};

// declarations
struct Decl : Node {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
    Word name;
    Decl(NodeKind kind, WordAndLocation name)
        : Node(kind, name.location), name(name) { VERIFY(isNodeType<Decl>(kind)); }
    SingleTokenSourceRange nameLocation() const { return packedToken(); }
};
// a type, namespace, function or static-variable declaration
struct StaticDecl : Decl {
    backwards_offset declArraysBegin;
    uint32_t withParamCount;
    uint32_t templateParamCount;
    uint32_t functionParamOrMemberCount;
    uint32_t staticDeclCount;

    StaticDecl(NodeKind kind, WordAndLocation name, TemplatedDeclArrays decls)
        : Decl(kind, name)
        , declArraysBegin(backwardsOffsetTo(decls.begin))
        , withParamCount(decls.withParameters().size())
        , templateParamCount(decls.templateParamters().size())
        , functionParamOrMemberCount(decls.callableParameters().size())
        , staticDeclCount(decls.statics().size()) {
        VERIFY(isNodeType<StaticDecl>(kind));
    }

    TemplatedDeclArrays decls() {
        return {
            {
                .begin = followBackwardsOffsetToArray(declArraysBegin),
                .parameterCount = withParamCount + templateParamCount + functionParamOrMemberCount,
                .staticCount = staticDeclCount,
            },
            withParamCount,
            templateParamCount,
        };
    }
};
struct VariableOrFunctionDecl : StaticDecl {

    VariableOrFunctionDecl(NodeKind kind, WordAndLocation name, TemplatedDeclArrays decls, Node* typeExpr, Node* bodyOrInitExpr)
        : StaticDecl(kind, name, decls)
        , m_returnOrTypeExpr(backwardsOffsetTo(typeExpr))
        , m_bodyOrInitExpr(backwardsOffsetTo(bodyOrInitExpr)) {
        VERIFY(matchNodeType<VariableOrFunctionDecl>(kind));
    }

    backwards_offset m_returnOrTypeExpr; // function return-type-expr or type-expr for variables
    backwards_offset m_bodyOrInitExpr; // function body-stmt or body-expr or init-expr for variables

    Node* returnOrTypeExpr() { return followBackwardsOffset(m_returnOrTypeExpr); }
    Node* bodyOrInitExpr() { return followBackwardsOffset(m_bodyOrInitExpr); }
};
// a parameter or (has-)member declaration
struct ParameterOrMemberDecl : Decl {
    backwards_offset m_typeExpr;
    backwards_offset m_initExpr; // has-member decls or init-expr for parameters and members

    ParameterOrMemberDecl(NodeKind kind, WordAndLocation name, Node* typeExpr, Node* initExpr)
        : Decl(kind, name)
        , m_typeExpr(backwardsOffsetTo(typeExpr))
        , m_initExpr(backwardsOffsetTo(initExpr)) {
        VERIFY(matchNodeType<ParameterOrMemberDecl>(kind));
    }

    Node* typeExpr() { return followBackwardsOffset(m_typeExpr); }
    Node* initExpr() { return followBackwardsOffset(m_initExpr); }
};

// statements
struct Stmt : Node {
    using Node::Node;
};
struct AssignStmt : Stmt {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    AssignStmt(SingleTokenSourceRange assignLoc)
        : Stmt(NodeKind::AssignStmt, assignLoc) { }
    SingleTokenSourceRange assignLocation() const { return packedToken(); }
};
struct UpdateStmt : Stmt {
    static constexpr int_t SUB_EXPRESSION_COUNT = 2;
    UpdateStmt(NodeKind kind, SingleTokenSourceRange operatorLoc)
        : Stmt(kind, operatorLoc) { VERIFY(matchNodeType<UpdateStmt>(kind)); }
    SingleTokenSourceRange operatorLocation() const { return packedToken(); }
};
struct LogicalUpdateStmt : Stmt {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    LogicalUpdateStmt(NodeKind kind, SingleTokenSourceRange operatorLoc)
        : Stmt(kind, operatorLoc) { VERIFY(matchNodeType<LogicalUpdateStmt>(kind)); }
    SingleTokenSourceRange operatorLocation() const { return packedToken(); }
};
struct LetStmt : Stmt {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
};
struct CompoundStmt : Stmt {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
    CompoundStmt(SingleTokenSourceRange leftLoc)
        : Stmt(NodeKind::CompoundStmt, leftLoc) { }
    SingleTokenSourceRange leftBraceLocation() const { return packedToken(); }
};
struct ExpressionStmt : Stmt {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    ExpressionStmt(SingleTokenSourceRange endToken)
        : Stmt(NodeKind::ExpressionStmt, endToken) { }
    SingleTokenSourceRange endTokenLocation() const { return packedToken(); }
};
struct IfStmt : Stmt {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    IfStmt(SingleTokenSourceRange ifLoc)
        : Stmt(NodeKind::IfStmt, ifLoc) { }
    SingleTokenSourceRange ifLocation() const { return packedToken(); }
};

// expressions
struct Expr : Node {
    using Node::Node;
};
struct UnaryOperatorExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    UnaryOperatorExpr(NodeKind kind, SingleTokenSourceRange operatorLoc)
        : Expr(kind, operatorLoc) { VERIFY(matchNodeType<UnaryOperatorExpr>(kind)); }
    SingleTokenSourceRange operatorLocation() const { return packedToken(); }
};
struct BinaryOperatorExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 2;
    BinaryOperatorExpr(NodeKind kind, SingleTokenSourceRange operatorLoc)
        : Expr(kind, operatorLoc) { VERIFY(matchNodeType<BinaryOperatorExpr>(kind)); }
    SingleTokenSourceRange operatorLocation() const { return packedToken(); }
};
struct BinaryLogicalOperatorExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    BinaryLogicalOperatorExpr(NodeKind kind, SingleTokenSourceRange operatorLoc)
        : Expr(kind, operatorLoc) { VERIFY(matchNodeType<BinaryLogicalOperatorExpr>(kind)); }
    SingleTokenSourceRange operatorLocation() const { return packedToken(); }
};
struct CallExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    CallExpr(NodeKind kind, SingleTokenSourceRange leftBracketLoc)
        : Expr(kind, leftBracketLoc) { VERIFY(matchNodeType<CallExpr>(kind)); }
    SingleTokenSourceRange leftBracketLocation() const { return packedToken(); }
};
struct ParenthesizedExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
    ParenthesizedExpr(SingleTokenSourceRange openParenLoc)
        : Expr(NodeKind::ParenthesizedExpr, openParenLoc) { }
    SingleTokenSourceRange leftParenthesizeLocation() const { return packedToken(); }
};
struct AccessExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    Word target;
    AccessExpr(NodeKind kind, SingleTokenSourceRange wordRange, Word target)
        : Expr(kind, wordRange), target(target) { VERIFY(matchNodeType<AccessExpr>(kind)); }
    SingleTokenSourceRange wordLocation() const { return packedToken(); }
    Word accessTarget() const { return target; }
};
struct IdentifierExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
    Word id;
    IdentifierExpr(SingleTokenSourceRange idLoc, Word id)
        : Expr(NodeKind::IdentifierExpr, idLoc), id(id) { }
    LocalSourceRange identifierLocation() const { return packedToken(); }
    Word identifierWord() const { return id; }
};
struct CompoundExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
    CompoundExpr(SingleTokenSourceRange leftAngleLoc)
        : Expr(NodeKind::CompoundExpr, leftAngleLoc) { }
    SingleTokenSourceRange leftAngleLocation() const { return packedToken(); }
};
struct IfExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    IfExpr(SingleTokenSourceRange ifLoc)
        : Expr(NodeKind::IfExpr, ifLoc) { }
    SingleTokenSourceRange ifLocation() const { return packedToken(); }
};
struct CommaElseExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    CommaElseExpr(SingleTokenSourceRange elseLoc)
        : Expr(NodeKind::CommaElseExpr, elseLoc) { }
    SingleTokenSourceRange elseLocation() const { return packedToken(); }
};
struct NumericLiteralExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
};
struct CharacterLiteralExpr : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
};
struct DesignateArgument : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    Word m_designator;
    DesignateArgument(SingleTokenSourceRange desLoc, Word des)
        : Expr(NodeKind::DesignateArgument, desLoc), m_designator(des) { }
    SingleTokenSourceRange designatorLocation() const { return packedToken(); }
    Word designatorWord() const { return m_designator; }
};
struct Parameterize : Expr {
    static constexpr int_t SUB_EXPRESSION_COUNT = 1;
    Parameterize(SingleTokenSourceRange leftBraceLoc)
        : Expr(NodeKind::Parameterize, leftBraceLoc) { }
    SingleTokenSourceRange leftBraceLocation() const { return packedToken(); }
};

void dump(Node*, const WordStringTable&);

template<typename T>
constexpr bool matchNodeType(NodeKind in) {
    switch (in) {

#define NODE(kind, type) \
    case NodeKind::kind: \
        return std::is_same_v<T, type>;
#include "nodes.h"

    case NodeKind::COUNT:
        VERIFY_NOT_REACHED();
    }
}
template<typename T>
constexpr bool isNodeType(NodeKind in) {
    switch (in) {

#define NODE(kind, type) \
    case NodeKind::kind: \
        return std::derived_from<type, T>;
#include "nodes.h"

    case NodeKind::COUNT:
        VERIFY_NOT_REACHED();
    }
}