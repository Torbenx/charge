#include "Parser.h"

const char* toString(StmtKind kind) {
#define STMT_KIND(kind)  \
    case StmtKind::kind: \
        return #kind;

    switch (kind) {
    case StmtKind::Invalid:
        return "Invalid";
        ENUMERATE_STMT_KINDS
    default:
        return "???";
    }

#undef STMT_KIND
};

const char* toString(DeclKind kind) {
#define DECL_KIND(kind)  \
    case DeclKind::kind: \
        return #kind;

    switch (kind) {
    case DeclKind::Invalid:
        return "Invalid";
        ENUMERATE_DECL_KINDS
    default:
        return "???";
    }

#undef DECL_KIND
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
const char* toShortString(AssignOperator op) {
    using enum AssignOperator;
    switch (op) {
    case None: return "=";
    case Plus: return "+=";
    case Minus: return "-=";
    case BitwiseAnd: return "&=";
    case BitwiseXor: return "^=";
    case BitwiseOr: return "|=";
    case Multiply: return "*=";
    case Divide: return "/=";
    case Remainder: return "%=";
    case ShiftLeft: return "<<=";
    case ShiftRight: return ">>=";
    default: return "???";
    }
}
// clang-format on

constexpr bool isWordOrGlobal(TokenKind kind) {
    return kind == TokenKind::Word || kind == TokenKind::ColonColon;
}

uint32_t Parser::allocate(uint32_t itemAlign, uint32_t itemSize, uint32_t itemCount) {
    uint32_t sizeAlign4 = alignmentCeil(sizeof(uint32_t), itemCount * itemSize) / sizeof(uint32_t);
    uint32_t alignmentAlign4 = alignmentCeil(sizeof(uint32_t), itemAlign) / sizeof(uint32_t);
    storageEndAlign4 = alignmentCeil(alignmentAlign4, storageEndAlign4);
    uint32_t ret = storageEndAlign4;
    storageEndAlign4 += sizeAlign4;
    return ret;
}

static uint64_t parseInteger(std::string_view range) {
    auto characterValue = [](uint8_t c) -> uint64_t {
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= '0' && c <= '9')
            return c - '0';
        VERIFY_NOT_REACHED();
    };

    uint64_t base = 10;
    uint64_t pos = 0;
    if (range[0] == '0') {
        if (range.length() == 1)
            return 0;
        if (range[1] == 'x' || range[1] == 'X')
            base = 16;
        else if (range[1] == 'b' || range[1] == 'B')
            base = 2;
        else
            VERIFY_NOT_REACHED();
        pos = 2;
    }
    uint64_t value = 0;
    for (; pos < range.length(); pos += 1) {
        if (range[pos] == '\'')
            continue;
        uint64_t v = characterValue(range[pos]);
        VERIFY(v < base);
        value = v + value * base;
    }
    return value;
}

void Parser::parseLeafExpr(Ptr<Expr>& out) {
    // unary op
    if (isUnaryOp(tok.kind())) {
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
    else if (tok.kind() == TokenKind::Word) {
        auto& e = makeSet<IdentifierExpr>(base, asWord(tok));
        advance();
        if (tok.kind() == TokenKind::LeftBrace)
            parseArgumentContext(e.identifier);
    }
    // IntegerLiteral
    else if (tok.kind() == TokenKind::IntegerLiteral) {
        base = make<IntLiteralExpr>(parseInteger(source.view(tok)));
        advance();
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
    } else if (tok.kind() == TokenKind::Point || tok.kind() == TokenKind::ColonColon) {
        bool isStatic = tok.kind() == TokenKind::ColonColon;
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::Word);
        auto& e = makeSet<AccessExpr>(base, isStatic, base, asWord(tok));
        advance();
        if (tok.kind() == TokenKind::LeftBrace)
            parseArgumentContext(e.member);
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

    auto args = beginSpan<Arguments::Arg>();
    while (tok.kind() != rightKind) {
        auto& arg = append(args, {});
        parseArgument(arg);
        VERIFY(tok.kind() == TokenKind::Comma || tok.kind() == rightKind);
        if (tok.kind() == TokenKind::Comma)
            advance();
    }
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

void Parser::parseLetStmt(Ptr<Stmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    if (source.view(tok) == "let")
        advance();

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    auto qual = VarDecl::Qualifier::None;
    if (source.view(tok) == "const") {
        qual = VarDecl::Qualifier::Const;
        advance();
    } else if (source.view(tok) == "mut") {
        qual = VarDecl::Qualifier::Mut;
        advance();
    }

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    auto d = make<VarDecl>(asWord(tok), qual);

    advance();
    if (tok.kind() == TokenKind::Colon) {
        advance();
        parseBinaryExpr(at(d).type);
    }
    EXPECT_EQ(tok.kind(), TokenKind::Equal);
    advance();
    parseBinaryExpr(at(d).initializer);

    out = make<LetStmt>(d);
}

void Parser::parseStmt(Ptr<Stmt>& out) {
    if (tok.kind() == TokenKind::SemiColon) {
        out = make<NullStmt>();
        return;
    }
    if (tok.kind() == TokenKind::Word) {
        auto view = source.view(tok);
        if (view == "let" || view == "const" || view == "mut")
            return parseLetStmt(out);
    }
    Ptr<Expr> expr;
    parseBinaryExpr(expr);
    if (isAssignOp(tok.kind())) {
        // AssignStmt
        auto& stmt = makeSet<AssignStmt>(out, tokenKindToAssignOp(tok.kind()), expr);
        advance();
        parseBinaryExpr(stmt.right);
    } else {
        // expression statement
        out = expr;
    }
}

void Parser::parseCompoundStmt(Ptr<CompoundStmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
    advance();
    auto& e = makeSet<CompoundStmt>(out);
    auto body = beginSpan<Ptr<Stmt>>();
    while (tok.kind() != TokenKind::RightBrace) {
        auto& stmt = append(body, {});
        parseStmt(stmt);
        EXPECT_EQ(tok.kind(), TokenKind::SemiColon);
        advance();
    }
    advance();
    e.body = finalizeSpan(body);
}

void Parser::parseParameterContext(Parameters& out) {
    TokenKind leftKind = tok.kind();
    VERIFY(isLeftBracket(leftKind));
    TokenKind rightKind = leftToRightBracket(leftKind);
    advance();

    auto params = beginSpan<Ptr<VarDecl>>();
    while (tok.kind() != rightKind) {
        auto& param = append(params, {});
        parseParameter(param);
        VERIFY(tok.kind() == TokenKind::Comma || tok.kind() == rightKind);
        if (tok.kind() == TokenKind::Comma)
            advance();
    }
    advance();
    out.params = finalizeSpan(params);
}

void Parser::parseParameter(Ptr<VarDecl>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    auto& e = makeSet<VarDecl>(out, asWord(tok));
    advance();
    if (tok.kind() == TokenKind::Colon) {
        advance();
        parseBinaryExpr(e.type);
    }
    if (tok.kind() == TokenKind::Equal) {
        advance();
        parseBinaryExpr(e.initializer);
    }
}

void Parser::parseWithClause(WithClause& out) {
    EXPECT_EQ(source.view(tok), "with");
    advance();
    parseParameterContext(out.params);
}

void Parser::parseDecl(Ptr<Decl>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    WithClause with;
    if (source.view(tok) == "with")
        parseWithClause(with);

    auto attributes = beginSpan<Word>();
    while (tok.kind() == TokenKind::Word) {
        append(attributes, asWord(tok));
        advance();
    }
    VERIFY(spanSize(attributes) > 0);
    Word name = *(spanEnd(attributes) - 1);

    Parameters parametric;
    if (tok.kind() == TokenKind::LeftBrace) {
        parseParameterContext(parametric);
    }

    // variable
    if (tok.kind() == TokenKind::Colon || tok.kind() == TokenKind::Equal) {
        auto& d = makeSet<VarDecl>(out);
        if (tok.kind() == TokenKind::Colon) {
            advance();
            parseBinaryExpr(d.type);
        }
        EXPECT_EQ(tok.kind(), TokenKind::Equal);
        advance();
        parseBinaryExpr(d.initializer);
        EXPECT_EQ(tok.kind(), TokenKind::SemiColon);
        advance();
    }
    // function
    else if (tok.kind() == TokenKind::LeftParen) {
        auto& d = makeSet<FnDecl>(out);
        parseParameterContext(d.params);
        EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
        parseCompoundStmt(d.body);
    }
    // struct
    else if (tok.kind() == TokenKind::LeftBrace) {
        advance();
        auto& d = makeSet<StructDecl>(out);
        auto decls = beginSpan<Ptr<Decl>>();
        while (tok.kind() != TokenKind::RightBrace) {
            auto& decl = append(decls, {});
            parseDecl(decl);
        }
        advance();
        d.decls = finalizeSpan(decls);
    }

    at(out).name = name;
    at(out).with = with;
    at(out).parametric = parametric;

    discardSpan(attributes);
}