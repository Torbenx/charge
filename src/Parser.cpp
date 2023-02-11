#include "Parser.h"
#include <iostream>
#include <vector>

const char* toString(NodeKind kind) {
#define NODE_KIND(kind)  \
    case NodeKind::kind: \
        return #kind;

    switch (kind) {
    case NodeKind::Invalid:
        return "Invalid";
        ENUMERATE_NODE_KINDS(NODE_KIND)
    default:
        return "???";
    }

#undef NODE_KIND
};

// clang-format off
const char* toShortString(UnaryOperator op) {
    using enum UnaryOperator;
    switch (op) {
    case BitwiseNot: return "~";
    case PreInc: return "++x";
    case PreDec: return "--x";
    case LogicalNot: return "!";
    case Plus: return "+";
    case Minus: return "-";
    case PostInc: return "x++";
    case PostDec: return "x--";
    default: return "???";
    }
}

const char* toShortString(BinaryOperator op) {
    using enum BinaryOperator;
    switch (op) {
    case Plus: return "+";
    case Minus: return "-";
    case NotEqual: return "!=";
    case Equal: return "==";
    case BitwiseAnd: return "&";
    case LogicalAnd: return "&&";
    case BitwiseXor: return "^";
    case BitwiseOr: return "|";
    case LogicalOr: return "||";
    case Multiply: return "*";
    case Divide: return "/";
    case Remainder: return "%";
    case Less: return "<";
    case ShiftLeft: return "<<";
    case LessEqual: return "<=";
    case Greater: return ">";
    case ShiftRight: return ">>";
    case GreaterEqual: return ">=";
    default: return "???";
    }
}

int precedenceOf(BinaryOperator op) {
    using enum BinaryOperator;
    switch (op) {
        case Multiply: return 1;
        case Divide: return 1;
        case Remainder: return 1;
        case Plus: return 2;
        case Minus: return 2;
        case ShiftLeft: return 3;
        case ShiftRight: return 3;
        case Less: return 4;
        case LessEqual: return 4;
        case Greater: return 4;
        case GreaterEqual: return 4;
        case NotEqual: return 5;
        case Equal: return 5;
        case BitwiseAnd: return 6;
        case BitwiseXor: return 7;
        case BitwiseOr: return 8;
        case LogicalAnd: return 9;
        case LogicalOr: return 10;
        default: VERIFY_NOT_REACHED();
    }
}
// clang-format on

uint32_t Parser::allocate(uint32_t itemAlign, uint32_t itemSize, uint32_t itemCount) {
    uint32_t sizeAlign4 = alignmentCeil(sizeof(uint32_t), itemCount * itemSize) / sizeof(uint32_t);
    uint32_t alignmentAlign4 = alignmentCeil(sizeof(uint32_t), itemAlign) / sizeof(uint32_t);
    storageEndAlign4 = alignmentCeil(alignmentAlign4, storageEndAlign4);
    uint32_t ret = storageEndAlign4;
    storageEndAlign4 += sizeAlign4;
    return ret;
}

void Parser::parseSimpleIdentifier(Identifier& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    auto elems = beginSpan<Word>();
    append(elems, asWord(tok));
    advance();
    out.global = false;
    out.elements = finalizeSpan(elems);
}
void Parser::parseParametricIdentifier(ParametricIdentifier& out) {
    parseSimpleIdentifier(out);
    bool hasBraces = tok.kind() == TokenKind::LeftBrace;
    out.hasBraces = hasBraces;
    if (hasBraces) {
        parseArgumentContext(out.args);
    }
}

void Parser::parseLeafExpr(Ptr<Expr>& out) {
    // unary op
    if (tok.isUnaryOp()) {
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
        parseArgumentContext(e.args);

    }
    // identifier
    else if (isWordOrGlobal(tok.kind())) {
        auto& e = makeSet<IdentifierExpr>(base);
        parseParametricIdentifier(e.identifier);
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
        auto& e = makeSet<CallExpr>(base, tok.kind() == TokenKind::LeftParen ? CallKind::Paren : CallKind::Angle, base);
        parseArgumentContext(e.args);
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

void Parser::parseBinaryExpr(Ptr<Expr>& out, int precedence) {
    Ptr<Expr> left;
    parseLeafExpr(left);

    while (isBinaryOp(tok.kind())) {
        BinaryOperator op2 = tokenKindToBinaryOp(tok.kind());
        int precedence2 = precedenceOf(op2);
        if (precedence <= precedence2)
            break;

        // op2 must be evaluated first
        advance();
        Ptr<Expr> right;
        parseBinaryExpr(right, precedence2);
        left = make<BinaryOperatorExpr>(op2, left, right);
    }

    out = left;
}

void Parser::parseArgumentContext(Arguments& out) {
    TokenKind leftKind = tok.kind();
    VERIFY(isLeftBracket(leftKind));
    TokenKind rightKind = leftToRightBracket(leftKind);

    advance();
    if (tok.kind() == rightKind) {
        advance();
        return;
    }

    auto args = beginSpan<Arguments::Arg>();
    do {
        auto& arg = append(args, {});
        parseArgument(arg);
        VERIFY(tok.kind() == TokenKind::Comma || tok.kind() == rightKind);
        if (tok.kind() == TokenKind::Comma)
            advance();
    } while (tok.kind() != rightKind);
    advance();
    out.args = finalizeSpan(args);
}

void Parser::parseArgument(Arguments::Arg& out) {
    if (tok.kind() == TokenKind::Point) {
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::Word);
        out.target = asWord(tok);
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::Equal);
        advance();
    }
    parseBinaryExpr(out.source);
}

struct Name {
    std::string_view name = {};
    bool withdot = false;

    Name withName(std::string_view name, bool withdot) {
        return { name, withdot };
    }
};
struct STDumper : STContext, STChildren<STDumper, Name>, STVisitor<STDumper> {
    std::ostream& out;
    std::vector<char> prefix = {};

    void dump(Ptr<Expr> e, Name name) {
        if (name.name.length() > 0) {
            if (name.withdot)
                out << '.';
            out << name.name << " = ";
        }
        out << toString(at(e).kind) << ' ';
        dispatchVisit(e);
        out << '\n';

        dispatchChildren(e, {});
    }

    void child(Ptr<Expr> e, IsLastChild last, Name name) {
        out << std::string_view { prefix.data(), prefix.size() };
        if ((bool)last) {
            out << "'-";
            prefix.push_back(' ');
        } else {
            out << "|-";
            prefix.push_back('|');
        }
        prefix.push_back(' ');

        dump(e, name);

        prefix.resize(prefix.size() - 2);
    }
    void child(Arguments args, IsLastChild last, Name) {
        for (uint32_t i = 0; i < args.args.count; i++) {
            auto arg = at(args.args, i);
            child(arg.source, (IsLastChild)((bool)last && i == args.args.count - 1), { sview(arg.target), true });
        }
    }

    void visitUnaryOperatorExpr(Ptr<UnaryOperatorExpr> e) {
        out << '\'' << toShortString(at(e).op) << '\'';
    }
    void visitAccessExpr(Ptr<AccessExpr> e) {
        out << "'." << sview(at(e).member) << '\'';
    }
    void visitCallExpr(Ptr<CallExpr> e) {
        out << '\'' << (at(e).callKind == CallKind::Paren ? "()" : "[]") << '\'';
    }
    void visitIdentifierExpr(Ptr<IdentifierExpr> e) {
        out << '\'';
        auto ident = at(e).identifier;
        if (ident.global)
            out << "::";
        out << sview(at(ident.elements, 0));
        for (uint32_t i = 1; i < ident.elements.count; i++)
            out << "::" << sview(at(ident.elements, i));
        out << '\'';
    }
    void visitBinaryOperatorExpr(Ptr<BinaryOperatorExpr> e) {
        out << '\'' << toShortString(at(e).op) << '\'';
    }
};

void dump(STContext context, Ptr<Expr> e) {
    STDumper dumper { context, {}, {}, std::cout };
    dumper.dump(e, {});
}