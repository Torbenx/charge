#pragma once

#include "StreamAllocator.h"
#include "WordTable.h"
#include "declarations.h"
#include "token.h"
#include <utility>

static constexpr auto words = ConstWordStringTable(
    // parser
    keyword("if"), keyword("else"), keyword("namespace"), keyword("struct"), keyword("object"), keyword("fn"),
    keyword("with"), keyword("template"), keyword("mut"), keyword("let"), keyword("inout"), keyword("out"),
    keyword("static"), keyword("return"), keyword("has"), keyword("as"),
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
    void setKind(NodeKind kind) { kindBits = std::to_underlying(kind); }
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
    relative_pointer<LetStmt, BlockVariableDecl> m_decl;
    LetStmt(SingleTokenSourceRange declaratorLoc)
        : Stmt(NodeKind::LetStmt, declaratorLoc) { }
    SingleTokenSourceRange declaratorLocation() const { return packedToken(); }

    void setDecl(BlockVariableDecl* decl) { m_decl = { this, decl }; }
    BlockVariableDecl* decl() { return m_decl.get(this); }
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
struct ReturnStmt : Stmt {
    static constexpr bool IMPLICIT_EXPRESSION_ARGUMENT = true;
    ReturnStmt(SingleTokenSourceRange returnLoc)
        : Stmt(NodeKind::ReturnStmt, returnLoc) { }
    SingleTokenSourceRange returnLocation() const { return packedToken(); }
};
struct EmptyReturnStmt : Stmt {
    static constexpr bool IMPLICIT_EXPRESSION_ARGUMENT = false;
    EmptyReturnStmt(SingleTokenSourceRange returnLoc)
        : Stmt(NodeKind::EmptyReturnStmt, returnLoc) { }
    SingleTokenSourceRange returnLocation() const { return packedToken(); }
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
    NumericLiteralExpr(SingleTokenSourceRange litLoc)
        : Expr(NodeKind::NumericLiteralExpr, litLoc) { }
    SingleTokenSourceRange literalLocation() const { return packedToken(); }
};
struct CharacterLiteralExpr : Expr {
    CharacterLiteralExpr(SingleTokenSourceRange litLoc)
        : Expr(NodeKind::CharacterLiteralExpr, litLoc) { }
    SingleTokenSourceRange literalRange() const { return packedToken(); }
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
void dump(Decl*, const WordStringTable&);

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