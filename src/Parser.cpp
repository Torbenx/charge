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
        case ValueEqual: return 5;
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
    return ret;
}

Word STContext::asWord(std::string_view view) {
    std::string word { view };
    uint32_t& id = storage->wordMap[std::move(word)];
    if (id == 0)
        id = storage->nextWordId++;
    return Word { id };
}

constexpr uint32_t UNARY_OP_WORD_BITS = (uint32_t)1 << 30;
constexpr uint32_t BINARY_OP_WORD_BITS = (uint32_t)2 << 30;
constexpr uint32_t ASSIGN_OP_WORD_BITS = (uint32_t)3 << 30;
constexpr uint32_t OP_WORD_MASK = (uint32_t)3 << 30;
Word STContext::makeUnaryOpWord(UnaryOperator op) {
    return Word { UNARY_OP_WORD_BITS | std::to_underlying(op) };
}
Word STContext::makeBinaryOpWord(BinaryOperator op) {
    return Word { BINARY_OP_WORD_BITS | std::to_underlying(op) };
}
Word STContext::makeAssignOpWord(BinaryOperator op) {
    return Word { ASSIGN_OP_WORD_BITS | std::to_underlying(op) };
}

std::string_view STContext::sview(Word word) const {
    switch (word.id >> 30) {
    case 0: {
        for (const auto& pair : storage->wordMap) {
            if (pair.second == word.id)
                return pair.first;
        }
        return {};
    }
    case 1:
        return toShortString((UnaryOperator)(word.id & ~OP_WORD_MASK));
    case 2: {
        std::string_view s = toStringWithEqual((BinaryOperator)(word.id & ~OP_WORD_MASK));
        s.remove_suffix(1);
        return s;
    }
    case 3:
        return toShortString((BinaryOperator)(word.id & ~OP_WORD_MASK));
    }
    VERIFY_NOT_REACHED();
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
    bool isMut = false;
    bool isConst = false;
    if (source.view(tok) == "const") {
        isConst = true;
        advance();
    } else if (source.view(tok) == "mut") {
        isMut = true;
        advance();
    }

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    Ptr<NamedDecl> decl;
    VarInfo* info;
    if (isConst)
        info = &makeSet<GlobalDecl>(decl, asWord(tok), true);
    else
        info = &makeSet<LocalDecl>(decl, asWord(tok), isMut);

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

void Parser::parseIfStmt(Ptr<Stmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    advance();

    auto& stmt = makeSet<IfStmt>(out);
    parseBinaryExpr(stmt.condition);
    parseSingleOrCompoundStmt(stmt.ifTrue);

    if (tok.kind() != TokenKind::Word || source.view(tok) != "else")
        return;
    advance();
    parseSingleOrCompoundStmt(stmt.ifFalse);
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
    EXPECT_EQ(tok.kind(), TokenKind::SemiColon);
    advance();
}

void Parser::parseStmt(Ptr<Stmt>& out) {
    if (tok.kind() == TokenKind::SemiColon) {
        out = make<NullStmt>();
        advance();
        return;
    }
    if (tok.kind() == TokenKind::Word) {
        auto view = source.view(tok);
        if (view == "let" || view == "const" || view == "mut")
            return parseLetStmt(out);
        if (view == "return")
            return parseReturnStmt(out);
        if (view == "if")
            return parseIfStmt(out);
    }
    parseExprOrAssignStmt(out);
}

void Parser::parseCompoundStmt(Ptr<CompoundStmt>& out) {
    EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
    advance();
    auto& e = makeSet<CompoundStmt>(out);
    auto body = beginSpan<Ptr<Stmt>>();
    while (tok.kind() != TokenKind::RightBrace) {
        auto& stmt = append(body, {});
        parseStmt(stmt);
    }
    advance();
    e.body = finalizeSpan(body);
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

void Parser::parseParameterContext(Parameters& out, ParameterParseScope scope) {
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
    out.params = (Span<Ptr<Decl>>)finalizeSpan(params);
}

void Parser::parseParameter(Ptr<LocalDecl>& out, ParameterParseScope scope) {
    // [mut] [name][&] [?constraint] [: type_or_constraint]
    // contraint:
    //   match expr
    //   expr
    // type_or_constraint:
    //   expr
    //   ?contraint

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    bool hasMut = source.view(tok) == "mut";
    if (hasMut) {
        VERIFY(scope == ParameterParseScope::Function);
        advance();
    }

    EXPECT_EQ(tok.kind(), TokenKind::Word);
    auto& e = makeSet<LocalDecl>(out, asWord(tok), /*isMut = */ hasMut);
    advance();
    if (tok.kind() == TokenKind::Amp) {
        VERIFY(scope == ParameterParseScope::Function);
        e.isInOut = true;
        advance();
    }
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
    parseParameterContext(out.params, ParameterParseScope::Static);
}

void Parser::parseDecl(Ptr<Decl>& out, DeclParseScope scope) {
    EXPECT_EQ(tok.kind(), TokenKind::Word);
    WithClause with;
    if (source.view(tok) == "with") {
        parseWithClause(with);
    }
    // has
    else if (source.view(tok) == "has") {
        VERIFY(scope == DeclParseScope::Struct);
        advance();
        auto& d = makeSet<HasDecl>(out);
        parseBinaryExpr(d.type);

        if (tok.kind() == TokenKind::SemiColon) {
            advance();
            return;
        }
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
    //
    if (source.view(tok) == "operation") {
        VERIFY(scope == DeclParseScope::Namespace);
        advance();

        bool hasAssign = false;
        EXPECT_EQ(tok.kind(), TokenKind::Word);
        if (source.view(tok) == "assign") {
            hasAssign = true;
            advance();
        }

        Word name = {};
        EXPECT_EQ(tok.kind(), TokenKind::Word);
        for (uint32_t i = 0; i < std::to_underlying(UnaryOperator::COUNT); i++) {
            if (source.view(tok) == toOperationString((UnaryOperator)i))
                name = makeUnaryOpWord((UnaryOperator)i);
        }
        for (uint32_t i = 0; i < std::to_underlying(BinaryOperator::FirstCmp); i++) {
            if (source.view(tok) == toOperationString((BinaryOperator)i))
                name = hasAssign ? makeAssignOpWord((BinaryOperator)i) : makeBinaryOpWord((BinaryOperator)i);
        }
        VERIFY((bool)name);
        advance();

        auto& d = makeSet<FnDecl>(out, name);
        parseParameterContext(d.params, ParameterParseScope::Function);
        EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
        parseCompoundStmt(d.body);
        return;
    }

    bool hasStatic = false;
    bool hasMut = false;
    Word declarator;
    Word name;
    while (tok.kind() == TokenKind::Word) {
        auto view = source.view(tok);
        if (view == "static")
            hasStatic = true;
        else if (view == "mut")
            hasMut = true;
        else {
            if (!declarator) {
                declarator = asWord(tok);
            } else {
                name = asWord(tok);
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
    switch (scope) {
    case DeclParseScope::Namespace:
        // [mut] name
        VERIFY(!hasStatic);
        break;
    case DeclParseScope::Struct:
        // static [mut] name
        // name
        VERIFY(!(hasMut && !hasStatic));
        break;
    case DeclParseScope::Has:
        // static [mut] name
        VERIFY(hasStatic);
        break;
    }

    Parameters parametric;
    if (tok.kind() == TokenKind::LeftBrace) {
        parseParameterContext(parametric, ParameterParseScope::Static);
    }

    bool isLocal = scope == DeclParseScope::Struct && !hasStatic;
    if (isLocal) {
        EXPECT_EQ(with.params.params.count, 0u);
        EXPECT_EQ(parametric.params.count, 0u);
    }

    // variable
    if (tok.kind() == TokenKind::Colon || tok.kind() == TokenKind::Equal) {
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
    else if (tok.kind() == TokenKind::LeftParen) {
        VERIFY(!hasMut);
        VERIFY(!hasStatic);
        auto& d = makeSet<FnDecl>(out, name);

        parseParameterContext(d.params, ParameterParseScope::Function);
        if (tok.kind() == TokenKind::Equal) {
            advance();
            EXPECT_EQ(tok.kind(), TokenKind::LeftParen);
            advance();

            parseParameter(d.assignParam, ParameterParseScope::Static);
            VERIFY(!(bool)at(d.assignParam).initializer);

            EXPECT_EQ(tok.kind(), TokenKind::RightParen);
            advance();
        }
        EXPECT_EQ(tok.kind(), TokenKind::LeftBrace);
        parseCompoundStmt(d.body);
    }
    // struct
    else if (tok.kind() == TokenKind::LeftBrace) {
        VERIFY(!hasMut);
        VERIFY(!hasStatic);
        advance();
        auto& d = makeSet<StructDecl>(out, name);
        auto memberDecls = beginSpan<Ptr<Decl>, 0>();
        auto staticDecls = beginSpan<Ptr<StaticDecl>, 1>();
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
        d.params.params = finalizeSpan(memberDecls);
        d.staticDecls = finalizeSpan(staticDecls);
    } else
        VERIFY_NOT_REACHED();

    if (auto staticDecl = asStaticDecl(out)) {
        at(staticDecl).with = with;
        at(staticDecl).parametric = parametric;
    }
}