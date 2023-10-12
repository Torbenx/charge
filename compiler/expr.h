#pragma once

#include "WordTable.h"
#include "token.h"
#include <utility>

enum class NodeKind : uint8_t {

#define NODE(kind, type) kind,
#include "nodes.h"

    COUNT,
};

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
        return std::derived_from<type, base>;
#include "nodes.h"

    case NodeKind::COUNT:
        VERIFY_NOT_REACHED();
    }
}

std::string_view nameString(NodeKind);

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

    struct backwards_offset {
        uint32_t offset;
    };
    backwards_offset backwardsOffsetTo(Node* target) const {
        int_t diff = this - target;
        VERIFY(diff >= 0);
        VERIFY(diff <= (int_t)(uint32_t)-1);
        return { (uint32_t)diff };
    }
    template<typename T>
    T* followBackwardOffset(backwards_offset offset) {
        Node* node = this - offset.offset;
        VERIFY(isNodeType<T>(node->kind));
        return (T*)node;
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

// statements
struct Stmt : Node {
    using Node::Node;
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

void dumpDecl(const Decl*, const WordStringTable&);
void dumpStmt(const Stmt*, const WordStringTable&);
void dumpExpr(const Expr*, const WordStringTable&);

struct Decl;
struct StaticDecl;
struct ParameterOrMemberDecl;
struct LetDecl;
struct LookupResult;

enum class LookupContextKind : uint32_t {
    Namespace,
    Type,
    PrematureType,
    Parameter,
    Block,
};
struct LookupResult {
    Decl* m_decl;
    id<Decl> decl() const {
        VERIFY((bool)m_decl);
        return m_decl;
    }
    bool found() const { return m_decl != nullptr; }
};
struct LookupContext {
    LookupContextKind kind;

    LookupResult lookup(Word);
};

struct Decl : Node {
    Word name;
    Decl(NodeKind kind, WordAndLocation name)
        : Node(kind, name.location), name(name) { VERIFY(isNodeType<Decl>(kind)); }
    SingleTokenSourceRange nameLocation() const { return packedToken(); }
};
// a type, namespace, function or static-variable declaration
struct StaticDecl : Decl {
    // the (return) type expr begins right after the parameter array
    backwards_offset paramsArrayBegin;
    uint32_t withParamCount;
    uint32_t templateParamCount;
    uint32_t functionParamOrMemberCount;

    backwards_offset bodyOrInitExpr; // function body or init-expr for variables
    backwards_offset staticDecls;
    uint32_t staticDeclCount;
};
// a parameter or (has-)member declaration
struct ParameterOrMemberDecl : Decl {
    // these are backwards offsets from this
    backwards_offset typeExprBeginOffset;
    backwards_offset declsOrInitExprBeginOffset; // has-member decls or init-expr for parameters and members

    ParameterOrMemberDecl(NodeKind kind, WordAndLocation name, Node* typeExpr, Node* initExpr)
        : Decl(kind, name)
        , typeExprBeginOffset(backwardsOffsetTo(typeExpr))
        , declsOrInitExprBeginOffset(backwardsOffsetTo(initExpr)) {
        VERIFY(matchNodeType<ParameterOrMemberDecl>(kind));
    }
};