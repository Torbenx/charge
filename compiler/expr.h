#pragma once

#include "StaticDeclProgram.h"
#include "StreamAllocator.h"
#include "WordTable.h"
#include "token.h"
#include <utility>

struct DeclArrayItem;
struct Decl;

static constexpr auto words = ConstWordStringTable(
    // parser
    keyword("if"), keyword("else"), keyword("namespace"), keyword("struct"), keyword("object"), keyword("fn"),
    keyword("with"), keyword("template"), keyword("mut"), keyword("let"), keyword("inout"), keyword("out"),
    keyword("static"),
    // sema
    "type");

using node_stream_offset = aligned_t<4>;

enum class ExpressionPrecedence : uint8_t {
    Primary,
    Postfix,
    Unary,
    Multiplication,
    Addition,
    Shift,
    Relational,
    Equality,
    BitwiseAnd,
    BitwiseXor,
    BitwiseOr,
    LogicalAnd,
    LogicalOr,
    ConditionalIf,
    ConditionalElse,
    Statement,
};

enum class NodeKind : uint8_t {

#define NODE(kind, type, prec) kind,
#include "nodes.h"

    COUNT,
};

ExpressionPrecedence precedenceOf(NodeKind node);
template<typename T>
constexpr bool matchNodeType(NodeKind in);
template<typename T>
constexpr bool isNodeType(NodeKind in);

std::string_view nameString(NodeKind);

struct Node {
    static constexpr int KIND_BITS = 8;
    static_assert(std::to_underlying(NodeKind::COUNT) < 1 << KIND_BITS);
    uint32_t kindBits : KIND_BITS;
    uint32_t streamOffsetBits : (32 - KIND_BITS);

    constexpr Node(NodeKind kind, SingleTokenSourceRange token)
        : kindBits(std::to_underlying(kind)), streamOffsetBits(token.tokenStreamOffset) { }
    SingleTokenSourceRange packedToken() const {
        return SingleTokenSourceRange(streamOffsetBits);
    }
    NodeKind kind() const { return NodeKind(kindBits); }

    using backwards_offset = node_stream_offset;
    constexpr backwards_offset backwardsOffsetTo(Node* target) const {
        if (target == nullptr)
            return { 0 };
        return backwards_offset::backwords_diff(target, this);
    }
    constexpr backwards_offset backwardsOffsetTo(DeclArrayItem* target) const {
        if (target == nullptr)
            return { 0 };
        return backwards_offset::backwords_diff(target, this);
    }
    Node* followBackwardsOffset(backwards_offset offset) {
        return (Node*)((std::byte*)this - offset);
    }
    DeclArrayItem* followBackwardsOffsetToArray(backwards_offset offset) {
        return (DeclArrayItem*)((std::byte*)this - offset);
    }
};

// declaration arrays
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
        constexpr Decl* operator[](int_t i) const { return (Decl*)((std::byte*)base - item[i].offset); }
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
struct Decl : Node {
    Word name;
    constexpr Decl(NodeKind kind, WordAndLocation name)
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

    StaticDeclProgram program;

    constexpr StaticDecl(NodeKind kind, WordAndLocation name, TemplatedDeclArrays decls)
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
struct TypeDecl : StaticDecl {
    constexpr TypeDecl(NodeKind kind, WordAndLocation name, TemplatedDeclArrays decls)
        : StaticDecl(kind, name, decls) { VERIFY(isNodeType<TypeDecl>(kind)); }
};
struct NamespaceDecl : StaticDecl {
    NamespaceDecl(WordAndLocation name, TemplatedDeclArrays decls)
        : StaticDecl(NodeKind::NamespaceDecl, name, decls) { }
};
struct ModuleDecl : StaticDecl {
    ModuleDecl(TemplatedDeclArrays decls)
        : StaticDecl(NodeKind::ModuleDecl, {}, decls) { }
};
struct FunctionDecl : StaticDecl {

    FunctionDecl(WordAndLocation name, TemplatedDeclArrays decls, Node* returnTypeExpr, Node* body)
        : StaticDecl(NodeKind::FunctionDecl, name, decls)
        , m_returnTypeExpr(backwardsOffsetTo(returnTypeExpr))
        , m_body(backwardsOffsetTo(body)) { }

    backwards_offset m_returnTypeExpr;
    backwards_offset m_body;

    Node* returnTypeExpr() { return followBackwardsOffset(m_returnTypeExpr); }
    Node* body() { return followBackwardsOffset(m_body); }
};
struct StaticVariableDecl : StaticDecl {

    StaticVariableDecl(NodeKind kind, WordAndLocation name, TemplatedDeclArrays decls, Node* typeExpr, Node* initExpr)
        : StaticDecl(kind, name, decls)
        , m_typeExpr(backwardsOffsetTo(typeExpr))
        , m_initExpr(backwardsOffsetTo(initExpr)) {
        VERIFY(matchNodeType<StaticVariableDecl>(kind));
    }

    backwards_offset m_typeExpr;
    backwards_offset m_initExpr;

    Node* typeExpr() { return followBackwardsOffset(m_typeExpr); }
    Node* initExpr() { return followBackwardsOffset(m_initExpr); }

    // operands for instructions in the constant stream
    InstructionOperand typeValue;
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
struct UpdateStmt : Stmt {
    static constexpr bool IMPLICIT_EXPRESSION_ARGUMENT = true;
    UpdateStmt(NodeKind kind, SingleTokenSourceRange operatorLoc)
        : Stmt(kind, operatorLoc) { VERIFY(matchNodeType<UpdateStmt>(kind)); }
    SingleTokenSourceRange operatorLocation() const { return packedToken(); }
};
struct LetStmt : Stmt {
    static constexpr bool IMPLICIT_EXPRESSION_ARGUMENT = false;
};
struct CompoundStmt : Stmt {
    static constexpr bool IMPLICIT_EXPRESSION_ARGUMENT = false;
    CompoundStmt(SingleTokenSourceRange leftLoc)
        : Stmt(NodeKind::CompoundStmt, leftLoc) { }
    SingleTokenSourceRange leftBraceLocation() const { return packedToken(); }
};
struct ExpressionStmt : Stmt {
    static constexpr bool IMPLICIT_EXPRESSION_ARGUMENT = true;
    ExpressionStmt(SingleTokenSourceRange endToken)
        : Stmt(NodeKind::ExpressionStmt, endToken) { }
    SingleTokenSourceRange endTokenLocation() const { return packedToken(); }
};
struct IfStmt : Stmt {
    static constexpr bool IMPLICIT_EXPRESSION_ARGUMENT = true;
    IfStmt(SingleTokenSourceRange ifLoc)
        : Stmt(NodeKind::IfStmt, ifLoc) { }
    SingleTokenSourceRange ifLocation() const { return packedToken(); }
};

// expressions
struct Expr : Node {
    using Node::Node;
};
struct UnaryOperatorExpr : Expr {
    UnaryOperatorExpr(NodeKind kind, SingleTokenSourceRange operatorLoc)
        : Expr(kind, operatorLoc) { VERIFY(matchNodeType<UnaryOperatorExpr>(kind)); }
    SingleTokenSourceRange operatorLocation() const { return packedToken(); }
};
struct BinaryOperatorExpr : Expr {
    BinaryOperatorExpr(NodeKind kind, SingleTokenSourceRange operatorLoc)
        : Expr(kind, operatorLoc) { VERIFY(matchNodeType<BinaryOperatorExpr>(kind)); }
    SingleTokenSourceRange operatorLocation() const { return packedToken(); }
};
struct CallExpr : Expr {
    CallExpr(NodeKind kind, SingleTokenSourceRange leftBracketLoc)
        : Expr(kind, leftBracketLoc) { VERIFY(matchNodeType<CallExpr>(kind)); }
    SingleTokenSourceRange leftBracketLocation() const { return packedToken(); }
};
struct ParenthesizedExpr : Expr {
    ParenthesizedExpr(SingleTokenSourceRange openParenLoc)
        : Expr(NodeKind::ParenthesizedExpr, openParenLoc) { }
    SingleTokenSourceRange leftParenthesizeLocation() const { return packedToken(); }
};
struct AccessExpr : Expr {
    Word target;
    AccessExpr(NodeKind kind, SingleTokenSourceRange wordRange, Word target)
        : Expr(kind, wordRange), target(target) { VERIFY(matchNodeType<AccessExpr>(kind)); }
    SingleTokenSourceRange wordLocation() const { return packedToken(); }
    Word accessTarget() const { return target; }
};
struct IdentifierExpr : Expr {
    Word id;
    IdentifierExpr(SingleTokenSourceRange idLoc, Word id)
        : Expr(NodeKind::IdentifierExpr, idLoc), id(id) { }
    LocalSourceRange identifierLocation() const { return packedToken(); }
    Word identifierWord() const { return id; }
};
struct CompoundExpr : Expr {
    CompoundExpr(SingleTokenSourceRange leftAngleLoc)
        : Expr(NodeKind::CompoundExpr, leftAngleLoc) { }
    SingleTokenSourceRange leftAngleLocation() const { return packedToken(); }
};
struct IfExpr : Expr {
    IfExpr(SingleTokenSourceRange ifLoc)
        : Expr(NodeKind::IfExpr, ifLoc) { }
    SingleTokenSourceRange ifLocation() const { return packedToken(); }
};
struct CommaElseExpr : Expr {
    CommaElseExpr(SingleTokenSourceRange elseLoc)
        : Expr(NodeKind::CommaElseExpr, elseLoc) { }
    SingleTokenSourceRange elseLocation() const { return packedToken(); }
};
struct NumericLiteralExpr : Expr {
};
struct CharacterLiteralExpr : Expr {
};
struct DesignateArgument : Expr {
    Word m_designator;
    DesignateArgument(SingleTokenSourceRange desLoc, Word des)
        : Expr(NodeKind::DesignateArgument, desLoc), m_designator(des) { }
    SingleTokenSourceRange designatorLocation() const { return packedToken(); }
    Word designatorWord() const { return m_designator; }
};
struct Parameterize : Expr {
    Parameterize(SingleTokenSourceRange leftBraceLoc)
        : Expr(NodeKind::Parameterize, leftBraceLoc) { }
    SingleTokenSourceRange leftBraceLocation() const { return packedToken(); }
};
struct EmptyNode : Node {
    static constexpr int_t SUB_EXPRESSION_COUNT = 0;
    EmptyNode(SingleTokenSourceRange loc)
        : Node(NodeKind::EmptyNode, loc) { }
    EmptyNode()
        : Node(NodeKind::EmptyNode, SingleTokenSourceRange(0)) { }
    SingleTokenSourceRange location() const { return packedToken(); }
};

void dump(Node*, const WordStringTable&);

template<typename T>
constexpr bool matchNodeType(NodeKind in) {
    switch (in) {

#define NODE(kind, type, prec) \
    case NodeKind::kind:       \
        return std::is_same_v<T, type>;
#include "nodes.h"

    default:
        VERIFY_NOT_REACHED();
    }
}
template<typename T>
constexpr bool isNodeType(NodeKind in) {
    switch (in) {

#define NODE(kind, type, prec) \
    case NodeKind::kind:       \
        return std::derived_from<type, T>;
#include "nodes.h"

    default:
        VERIFY_NOT_REACHED();
    }
}