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

const char* toString(ExprKind kind) {
#define EXPR_KIND(kind)  \
    case ExprKind::kind: \
        return #kind;

    switch (kind) {
    case ExprKind::Invalid:
        return "Invalid";
        ENUMERATE_EXPR_KINDS
    default:
        return "???";
    }

#undef EXPR_KIND
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
const char* toOperationString(UnaryOperator op) {
    using enum UnaryOperator;
    switch (op) {
    case BitwiseNot: return "BitNot";
    case PreInc: return "PreInc";
    case PreDec: return "PreDec";
    case LogicalNot: return "LogNot";
    case Plus: return "Plus";
    case Minus: return "Neg";
    case PostInc: return "PostInc";
    case PostDec: return "PostDec";
    default: VERIFY_NOT_REACHED();
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
const char* toStringWithEqual(BinaryOperator op) {
    using enum BinaryOperator;
    switch (op) {
    case Plus: return "+=";
    case Minus: return "-=";
    case BitwiseAnd: return "&=";
    case LogicalAnd: return "&&=";
    case BitwiseXor: return "^=";
    case BitwiseOr: return "|=";
    case LogicalOr: return "||=";
    case Multiply: return "*=";
    case Divide: return "/=";
    case Remainder: return "%=";
    case ShiftLeft: return "<<=";
    case ShiftRight: return ">>=";
    default: VERIFY_NOT_REACHED();
    }
}
const char* toOperationString(BinaryOperator op) {
    using enum BinaryOperator;
    switch (op) {
    case Plus: return "Add";
    case Minus: return "Sub";
    case BitwiseAnd: return "BitAnd";
    case LogicalAnd: return "LogAnd";
    case BitwiseXor: return "BitXor";
    case BitwiseOr: return "BitOr";
    case LogicalOr: return "LogOr";
    case Multiply: return "Mul";
    case Divide: return "Div";
    case Remainder: return "Rem";
    case ShiftLeft: return "Shl";
    case ShiftRight: return "Shr";
    default: VERIFY_NOT_REACHED();
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

uint32_t STStorage::allocate(uint32_t itemAlign, uint32_t itemSize, uint32_t itemCount) {
    uint32_t sizeAlign4 = alignmentCeil(sizeof(uint32_t), itemCount * itemSize) / sizeof(uint32_t);
    uint32_t alignmentAlign4 = alignmentCeil(sizeof(uint32_t), itemAlign) / sizeof(uint32_t);
    storageEndAlign4 = alignmentCeil(alignmentAlign4, storageEndAlign4);
    uint32_t ret = storageEndAlign4;
    storageEndAlign4 += sizeAlign4;
    VERIFY(storageEndAlign4 <= storageArraySizeAlign4);
    return ret;
}

Word STContext::asWord(std::string_view view) {
    std::string word { view };
    uint32_t& id = storage->wordMap[std::move(word)];
    if (id == 0)
        id = storage->nextWordId++;
    return Word { id };
}

std::string_view STContext::sview(Word word) const {
    for (const auto& pair : storage->wordMap) {
        if (pair.second == word.id)
            return pair.first;
    }
    return {};
}

uint64_t Parser::lexInteger() {
    VERIFY(tok.kind() == TokenKind::IntegerLiteral);

    auto characterValue = [](uint8_t c) -> int {
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= '0' && c <= '9')
            return c - '0';

        if (c == '\'')
            return -2;

        return -1;
    };

    int curDig = characterValue(source[pos()]);
    auto nextDigit = [&]() {
        m_position += 1;
        curDig = characterValue(source[pos()]);
    };

    uint64_t base = 10;
    if (curDig == 0) {
        nextDigit();
        char baseChar = source[pos()];
        if (baseChar == 'x' || baseChar == 'X') {
            base = 16;
            nextDigit();
        } else if (baseChar == 'b' || baseChar == 'B') {
            base = 2;
            nextDigit();
        }
    }
    uint64_t value = 0;
    for (; curDig != -1; nextDigit()) {
        if (curDig == -2)
            continue;
        VERIFY((uint64_t)curDig < base);
        value = value * base + (uint64_t)curDig;
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
    // constraint
    if (tok.kind() == TokenKind::Question) {
        auto& e = makeSet<ConstraintExpr>(out);
        advance();
        return parseLeafExpr(e.constraint.condition);
    }

    Ptr<Expr> base;
    // paren
    if (tok.kind() == TokenKind::LeftParen) {
        auto& e = makeSet<ParenExpr>(base);
        parseArgumentContext(e.args);
    }
    // identifier
    else if (tok.kind() == TokenKind::Word) {
        auto& e = makeSet<IdentifierExpr>(base, tok.word);
        advance();
        if (tok.kind() == TokenKind::LeftBrace) {
            e.identifier.hasBraces = true;
            parseArgumentContext(e.identifier);
        }
    }
    // integer literal
    else if (tok.kind() == TokenKind::IntegerLiteral) {
        base = make<IntLiteralExpr>(lexInteger());
        skipWhiteSpace();
        advance();
    }
    // compound expr
    else if (tok.kind() == TokenKind::LeftAngle) {
        auto& e = makeSet<CompoundExpr>(base);
        e.body = parseStmts();
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
        auto& e = makeSet<AccessExpr>(base, isStatic, base, tok.word);
        advance();
        if (tok.kind() == TokenKind::LeftBrace) {
            e.member.hasBraces = true;
            parseArgumentContext(e.member);
        }
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
        out.target = tok.word;
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::Equal);
        advance();
    }
    parseBinaryExpr(out.source);
}

void Parser::parseLetStmt(Ptr<Stmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    if (tok.word == letWord)
        advance();

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    bool isMut = false;
    bool isStatic = false;
    if (tok.word == staticWord) {
        isStatic = true;
        advance();
    }
    if (tok.word == mutWord) {
        isMut = true;
        advance();
    }

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    Ptr<NamedDecl> decl;
    VarInfo* info;
    if (isStatic)
        info = &makeSet<GlobalDecl>(decl, tok.word, isMut);
    else
        info = &makeSet<LocalDecl>(decl, tok.word, isMut);

    advance();
    if (tok.kind() == TokenKind::Colon) {
        advance();
        parseBinaryExpr(info->type);
    }
    EXPECT_EQ(tok.kind(), TokenKind::Equal);
    advance();
    parseBinaryExpr(info->initializer);

    out = make<LetStmt>(decl);

    EXPECT_EQ(tok.kind(), TokenKind::SemiColon);
    advance();
}

void Parser::parseReturnStmt(Ptr<Stmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    advance();
    auto& e = makeSet<ReturnStmt>(out);
    if (tok.kind() == TokenKind::SemiColon)
        return;
    parseBinaryExpr(e.expr);

    EXPECT_EQ(tok.kind(), TokenKind::SemiColon);
    advance();
}

void Parser::parseIfBranch(Ptr<Stmt>& out) {
    auto& stmt = makeSet<IfStmt>(out);
    parseBinaryExpr(stmt.condition);
    EXPECT_EQ(tok.kind(), TokenKind::Colon);
    advance();
    parseSingleOrCompoundStmt(stmt.ifTrue);

    if (tok.kind() != TokenKind::Word)
        return;
    if (tok.word == elseWord) {
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::Colon);
        advance();
        parseSingleOrCompoundStmt(stmt.ifFalse);
    } else if (tok.word == elifWord) {
        advance();
        parseIfBranch(stmt.ifFalse);
    }
}
void Parser::parseIfStmt(Ptr<Stmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    advance();
    parseIfBranch(out);
}

void Parser::parseForStmt(Ptr<Stmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    advance();
    auto& stmt = makeSet<ForStmt>(out);
    parseParameter(stmt.loopVarDecl, ParameterParseScope::Function);
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    VERIFY(tok.word == inWord);
    advance();
    parseBinaryExpr(stmt.rangeExpr);
    EXPECT_EQ(tok.kind(), TokenKind::Colon);
    advance();
    parseSingleOrCompoundStmt(stmt.body);
}

void Parser::parseExprOrAssignStmt(Ptr<Stmt>& out) {
    Ptr<Expr> expr;
    parseBinaryExpr(expr);
    if (isAssignOp(tok.kind()) || tok.kind() == TokenKind::Equal) {
        // AssignStmt
        auto& stmt = makeSet<AssignStmt>(out, tokenKindToAssignOp(tok.kind()), expr);
        advance();
        parseBinaryExpr(stmt.right);
    } else {
        // expression statement
        out = make<ExprStmt>(expr);
    }
    // semi colon not mandatory to support [expr]
    if (tok.kind() == TokenKind::SemiColon)
        advance();
}

void Parser::parseStmt(Ptr<Stmt>& out) {
    if (tok.kind() == TokenKind::SemiColon) {
        out = make<NullStmt>();
        advance();
        return;
    }
    if (tok.kind() == TokenKind::Word) {
        if (tok.word == letWord || tok.word == staticWord || tok.word == mutWord)
            return parseLetStmt(out);
        if (tok.word == returnWord)
            return parseReturnStmt(out);
        if (tok.word == ifWord)
            return parseIfStmt(out);
        if (tok.word == forWord)
            return parseForStmt(out);
    }
    parseExprOrAssignStmt(out);
}

Span<Ptr<Stmt>> Parser::parseStmts() {
    VERIFY(isLeftBracket(tok.kind()));
    TokenKind rightBracket = leftToRightBracket(tok.kind());
    advance();
    auto body = beginSpan<Ptr<Stmt>>();
    while (tok.kind() != rightBracket) {
        auto& stmt = append(body, {});
        parseStmt(stmt);
    }
    advance();
    return finalizeSpan(body);
}

void Parser::parseCompoundStmt(Ptr<CompoundStmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
    auto& e = makeSet<CompoundStmt>(out);
    e.body = parseStmts();
}

void Parser::parseSingleOrCompoundStmt(Ptr<Stmt>& out) {
    if (tok.kind() == TokenKind::LeftBrace) {
        Ptr<CompoundStmt> body;
        parseCompoundStmt(body);
        out = body;
    } else {
        parseStmt(out);
    }
}

Span<Ptr<LocalDecl>> Parser::parseParameterContext(ParameterParseScope scope) {
    TokenKind leftKind = tok.kind();
    VERIFY(isLeftBracket(leftKind));
    TokenKind rightKind = leftToRightBracket(leftKind);
    advance();

    auto params = beginSpan<Ptr<LocalDecl>>();
    while (tok.kind() != rightKind) {
        auto& param = append(params, {});
        parseParameter(param, scope);
        VERIFY(tok.kind() == TokenKind::Comma || tok.kind() == rightKind);
        if (tok.kind() == TokenKind::Comma)
            advance();
    }
    advance();
    return finalizeSpan(params);
}

void Parser::parseParameter(Ptr<LocalDecl>& out, ParameterParseScope scope) {
    // [mut] [name][&] [?constraint] [: expr]

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    bool hasMut = tok.word == mutWord;
    if (hasMut) {
        VERIFY(scope == ParameterParseScope::Function);
        advance();
    }

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    auto& e = makeSet<LocalDecl>(out, tok.word, /*isMut = */ hasMut);
    advance();
    if (tok.kind() == TokenKind::Amp) {
        VERIFY(scope == ParameterParseScope::Function);
        e.isInOut = true;
        advance();
    }
    auto constr = beginSpan<Constraint>();
    while (tok.kind() == TokenKind::Question) {
        advance();
        auto& c = append(constr, {});
        parseLeafExpr(c.condition);
    }
    e.valueConstraints = finalizeSpan(constr);
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
    VERIFY(tok.word == withWord);
    advance();
    out.params = parseParameterContext(ParameterParseScope::Static);
}

void Parser::parseFunctionDefinition(FnDecl& out) {
    if (tok.kind() == TokenKind::SemiColon)
        return;
    if (tok.kind() == TokenKind::FatArrow) {
        advance();
        parseBinaryExpr(out.bodyExpr);
        EXPECT_EQ(tok.kind(), TokenKind::SemiColon);
        advance();
    } else if (tok.kind() == TokenKind::Equal) {
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::LeftParen);
        advance();

        parseParameter(out.assignParam, ParameterParseScope::Static);
        VERIFY(!(bool)at(out.assignParam).initializer);

        EXPECT_EQ(tok.kind(), TokenKind::RightParen);
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::Colon);
        advance();
        parseSingleOrCompoundStmt(out.body);
    } else if (tok.kind() == TokenKind::Colon) {
        advance();
        parseSingleOrCompoundStmt(out.body);
    } else
        VERIFY_NOT_REACHED();
}

void Parser::parseDecl(Ptr<Decl>& out, DeclParseScope scope) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    WithClause with;
    Span<Ptr<LocalDecl>> templateParams;
    if (tok.word == withWord) {
        parseWithClause(with);
    }
    if (tok.word == templateWord) {
        advance();
        templateParams = parseParameterContext(ParameterParseScope::Static);
    }
    // has
    else if (tok.word == hasWord) {
        VERIFY(scope == DeclParseScope::Struct);
        advance();
        auto& d = makeSet<HasDecl>(out);
        parseBinaryExpr(d.type);

        if (tok.kind() == TokenKind::SemiColon) {
            advance();
            return;
        }
        EXPECT_EQ(tok.kind(), TokenKind::Colon);
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
        advance();
        auto decls = beginSpan<Ptr<StaticDecl>>();
        while (tok.kind() != TokenKind::RightBrace) {
            Ptr<Decl> decl = {};
            parseDecl(decl, DeclParseScope::Has);
            VERIFY(isStaticDecl(decl));
            append(decls, (Ptr<StaticDecl>)decl);
        }
        advance();
        d.decls = finalizeSpan(decls);
        return;
    }

    bool hasStatic = false;
    bool hasMut = false;
    Word declarator;
    Word name;
    while (tok.kind() == TokenKind::Word) {
        if (tok.word == staticWord)
            hasStatic = true;
        else if (tok.word == mutWord)
            hasMut = true;
        else {
            if (!declarator) {
                declarator = tok.word;
            } else {
                name = tok.word;
                advance();
                break;
            }
        }
        advance();
    }
    if (!name) {
        name = declarator;
        declarator = {};
    }
    VERIFY((bool)name);

    bool isLocal = scope == DeclParseScope::Struct && !hasStatic;
    if (isLocal) {
        EXPECT_EQ(with.params.count, 0u);
        EXPECT_EQ(templateParams.count, 0u);
    }

    // variable
    if (!declarator) {
        if (!hasMut && !hasStatic && (tok.kind() == TokenKind::SemiColon || tok.kind() == TokenKind::Equal)) {
            auto& e = makeSet<EnumValueDecl>(out, name);
            if (tok.kind() == TokenKind::Equal) {
                advance();
                parseBinaryExpr(e.enumValue);
            }
            EXPECT_EQ(tok.kind(), TokenKind::SemiColon);
            advance();
            return;
        }
        VarInfo* info;
        if (isLocal)
            info = &makeSet<LocalDecl>(out, name, hasMut);
        else
            info = &makeSet<GlobalDecl>(out, name, !hasMut);

        if (tok.kind() == TokenKind::Colon) {
            advance();
            parseBinaryExpr(info->type);
        }
        if (tok.kind() == TokenKind::Equal) {
            advance();
            parseBinaryExpr(info->initializer);
        } else
            VERIFY(scope == DeclParseScope::Struct);

        EXPECT_EQ(tok.kind(), TokenKind::SemiColon);
        advance();
    }
    // function
    else if (declarator == fnWord) {
        VERIFY(!hasMut);
        VERIFY(!hasStatic);
        auto& d = makeSet<FnDecl>(out, name);

        d.params = (Span<Ptr<Decl>>)parseParameterContext(ParameterParseScope::Function);
        parseFunctionDefinition(d);
    }
    // struct
    else if (declarator == structWord) {
        VERIFY(!hasMut);
        VERIFY(!hasStatic);
        auto& d = makeSet<StructDecl>(out, name);
        auto memberDecls = beginSpan<Ptr<Decl>, 0>();
        auto staticDecls = beginSpan<Ptr<StaticDecl>, 1>();
        EXPECT_EQ(tok.kind(), TokenKind::Colon);
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
        advance();
        while (tok.kind() != TokenKind::RightBrace) {
            Ptr<Decl> decl;
            parseDecl(decl, DeclParseScope::Struct);
            if (Ptr<StaticDecl> sDecl = asStaticDecl(decl)) {
                append(staticDecls, sDecl);
            } else {
                VERIFY(at(decl).kind == DeclKind::LocalDecl || at(decl).kind == DeclKind::HasDecl);
                append(memberDecls, (Ptr<Decl>)decl);
            }
        }
        advance();
        d.params = finalizeSpan(memberDecls);
        d.staticDecls = finalizeSpan(staticDecls);
    }
    // namespace
    else if (declarator == namespaceWord) {
        auto& d = makeSet<NamespaceDecl>(out, name);
        auto staticDecls = beginSpan<Ptr<StaticDecl>>();
        EXPECT_EQ(tok.kind(), TokenKind::Colon);
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
        advance();
        while (tok.kind() != TokenKind::RightBrace) {
            Ptr<Decl> decl;
            parseDecl(decl, DeclParseScope::Namespace);
            VERIFY(isStaticDecl(decl));
            append(staticDecls, (Ptr<StaticDecl>)decl);
        }
        advance();
        d.staticDecls = finalizeSpan(staticDecls);
    } else if (declarator == enumWord) {
        auto& d = makeSet<EnumDecl>(out, name);
        auto staticDecls = beginSpan<Ptr<StaticDecl>>();
        EXPECT_EQ(tok.kind(), TokenKind::Colon);
        advance();
        EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
        advance();
        while (tok.kind() != TokenKind::RightBrace) {
            Ptr<Decl> decl;
            parseDecl(decl, DeclParseScope::Namespace);
            VERIFY(isStaticDecl(decl));
            append(staticDecls, (Ptr<StaticDecl>)decl);
        }
        advance();
        d.staticDecls = finalizeSpan(staticDecls);
    } else
        VERIFY_NOT_REACHED();

    if (auto staticDecl = asStaticDecl(out)) {
        at(staticDecl).with = with;
        at(staticDecl).templateParams = templateParams;
    }
}