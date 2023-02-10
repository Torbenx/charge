#include "Parser.h"

uint32_t Parser::allocate(uint32_t itemAlign, uint32_t itemSize, uint32_t itemCount) {
    uint32_t sizeAlign4 = alignmentCeil(sizeof(uint32_t), itemCount * itemSize) / sizeof(uint32_t);
    uint32_t alignmentAlign4 = alignmentCeil(sizeof(uint32_t), itemAlign) / sizeof(uint32_t);
    storageEndAlign4 = alignmentCeil(alignmentAlign4, storageEndAlign4);
    uint32_t ret = storageEndAlign4;
    storageEndAlign4 += sizeAlign4;
    return ret;
}

void Parser::parseNestedNameOrType(Ptr<Expr>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    makeSet<IdentifierExpr>(out, asWord(tok));
    advance();
}

// trailing binary operators do not belong to us
void Parser::parseLeafExpr(Ptr<Expr>& out) {
    // unary op
    if (tok.isOperator()) {
        VERIFY(tok.isUnaryOp());
        auto& e = makeSet<UnaryOperatorExpr>(out, tokenKindToUnaryOp(tok.kind()));
        advance();
        return parseLeafExpr(e.subExpr);
    }

    Ptr<Expr> base;
    // paren
    if (tok.kind() == TokenKind::LeftParen) {
        auto& e = makeSet<ParenExpr>(base);
        advance();
        parseBinaryExpr(e.subExpr);

        EXPECT_EQ(tok.kind(), TokenKind::RightParen);
        advance();
    }
    // brace
    else if (tok.kind() == TokenKind::LeftBrace) {
        auto& e = makeSet<ImmediateBraceExpr>(base);
        advance();
        parseArgumentContext(e.args);
        EXPECT_EQ(tok.kind(), TokenKind::RightBrace);
        advance();
    }
    // identifier
    else if (isWordOrGlobal(tok.kind())) {
        parseNestedNameOrType(base);
    }
    //
    else {
        fmt::println("unexpected token {}", tok.kind());
        VERIFY_NOT_REACHED();
    }

    // handle postfix
    return wrapWithPostfixes(out, base);
}

void Parser::wrapWithPostfixes(Ptr<Expr>& out, Ptr<Expr> base) {
    if (tok.kind() == TokenKind::LeftParen || tok.kind() == TokenKind::LeftAngle) {
        TokenKind kind = tok.kind();
        advance();
        auto& e = makeSet<CallExpr>(base, tok.kind() == TokenKind::LeftParen ? CallKind::Paren : CallKind::Angle, base);
        parseArgumentContext(e.args);
        EXPECT_EQ(tok.kind(), leftToRightBracket(kind));
        advance();
    } else if (tok.kind() == TokenKind::Point) {
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::Word);
        base = make<AccessExpr>(base, asWord(tok));
        advance();
    } else if (tok.kind() == TokenKind::PlusPlus || tok.kind() == TokenKind::MinusMinus) {
        UnaryOperator op = tok.kind() == TokenKind::PlusPlus ? UnaryOperator::PostInc : UnaryOperator::PostDec;
        base = make<UnaryOperatorExpr>(op, base);
        advance();
    } else {
        out = base;
        return;
    }

    return wrapWithPostfixes(out, base);
}

void Parser::parseBinaryExpr(Ptr<Expr>& out) {
    parseLeafExpr(out);
}

void Parser::parseArgumentContext(Arguments&) {
    VERIFY_NOT_REACHED();
}

void Parser::dumpTree(Ptr<Expr> e, int indent) {
    using enum ExprKind;
    auto toString = [](ExprKind kind) {
        switch (kind) {
        case Invalid:
            return "InvalidExpr";
        case UnaryOperator:
            return "UnaryOperatorExpr";
        case Paren:
            return "ParenExpr";
        case Access:
            return "AccessExpr";
        case ImmediateBrace:
            return "ImmediateBraceExpr";
        case Call:
            return "CallExpr";
        case Identifier:
            return "IdentifierExpr";
        }
    };
    fmt::println("{:{}}{}", "", indent * 2, toString(at(e).kind));
    if (at(e).kind == UnaryOperator)
        dumpTree(as<UnaryOperatorExpr>(e).subExpr, indent + 1);
    if (at(e).kind == Paren)
        dumpTree(as<ParenExpr>(e).subExpr, indent + 1);
    if (at(e).kind == Access)
        dumpTree(as<AccessExpr>(e).subExpr, indent + 1);
}