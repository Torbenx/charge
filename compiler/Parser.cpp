#include "Parser.h"

template<bool (*test)(NodeKind)>
static constexpr NodeKind firstInstance() {
    for (std::underlying_type_t<NodeKind> i = 0; i < std::to_underlying(NodeKind::COUNT); i++) {
        NodeKind kind = (NodeKind)i;
        if (test(kind))
            return kind;
    }
    VERIFY_NOT_REACHED();
}

static void consumeSemiColon(Parser* par) {
    if (par->tok == Token::SemiColon)
        par->nextToken();
    else
        par->errorHandler->expectedSemiColon(par);
}

std::string_view nameString(NodeKind kind) {
    switch (kind) {

#define NODE(kind, type, prec) \
    case NodeKind::kind:       \
        return #kind;
#include "nodes.h"

    default:
        VERIFY_NOT_REACHED();
    }
}
ExpressionPrecedence precedenceOf(NodeKind node) {
    switch (node) {

#define NODE(kind, type, prec) \
    case NodeKind::kind:       \
        return ExpressionPrecedence::prec;
#include "nodes.h"

    default:
        VERIFY_NOT_REACHED();
    }
}

std::string_view nameString(DeclKind kind) {
    switch (kind) {

#define ANY_DECL(kind, type) \
    case DeclKind::kind:     \
        return #kind;
#include "declarations.inc"

    default:
        VERIFY_NOT_REACHED();
    }
}

void StaticDeclContext::addDecl(StaticDecl* decl) {
    decl->setDeclaringStaticDecl(DeclaringStaticDecl::fromContext(this));
    auto result = findWord(decl->name);
    VERIFY(!result.found);
    entries[result.bucket] = { decl->name, std::bit_cast<uint32_t>(relative_t(this, decl)) };
    usedBuckets += 1;
    maybeRehash();
}
template<std::derived_from<Decl> T, typename... Args>
T* Parser::emitDeclInternal(Args&&... args) {
    if constexpr (requires { { T::DECL_PROGRAM_SIZE } -> std::convertible_to<int_t>; }) {
        auto* prog = (DeclProgram*)nodeStream.allocate(8, T::DECL_PROGRAM_SIZE);
        return std::construct_at(nodeStream.template allocate<T>(), prog, std::forward<Args>(args)...);
    } else {
        return std::construct_at(nodeStream.template allocate<T>(), std::forward<Args>(args)...);
    }
}
template<std::derived_from<Decl> T, typename... Args>
T* Parser::emitDecl(Args&&... args) {
    T* decl = emitDeclInternal<T, Args...>(std::forward<Args>(args)...);

    if constexpr (std::derived_from<T, ParameterOrMemberDecl>)
        parameterDeclContext->addDecl(decl);
    else if constexpr (std::derived_from<T, StaticDecl>)
        staticDeclContext->addDecl(decl);

    if (instrumenter)
        instrumenter->emitDecl(this, decl);
    return decl;
}

struct StaticDeclContextHelper {
    Parser* parser;
    StaticDeclContext* prevStaticDeclContext;
    StaticDeclContextHelper(Parser* parser, StaticDeclContext* newContext)
        : parser(parser), prevStaticDeclContext(parser->staticDeclContext) {
        parser->staticDeclContext = newContext;
    }
    ~StaticDeclContextHelper() {
        parser->staticDeclContext = prevStaticDeclContext;
    }
};
struct Parser::ParameterDeclContextHelper {
    Parser* parser;
    ParameterDeclContext* prevParamtererDeclContext;
    ParameterDeclContextHelper(Parser* parser)
        : parser(parser), prevParamtererDeclContext(parser->parameterDeclContext) {
        parser->parameterDeclContext = std::construct_at(parser->nodeStream.allocate<ParameterDeclContext>());
    }
    ParameterDeclContextHelper(ParameterDeclContextHelper&& other)
        : parser(other.parser), prevParamtererDeclContext(other.prevParamtererDeclContext) {
        other.parser = nullptr;
    }
    ParameterDeclContext* popContext() {
        VERIFY(parser != nullptr);
        ParameterDeclContext* context = parser->parameterDeclContext;
        parser->parameterDeclContext = prevParamtererDeclContext;
        return context;
    }
    ParameterDeclContext* get() { return parser->parameterDeclContext; }
    ~ParameterDeclContextHelper() {
        if (parser)
            parser->parameterDeclContext = prevParamtererDeclContext;
    }
};

// declarations
NamespaceDecl* Parser::parseModule() {
    VERIFY(staticDeclContext == nullptr);
    VERIFY(parameterDeclContext == nullptr);
    NamespaceDecl* decl = emitDeclInternal<NamespaceDecl>(WordAndLocation());
    StaticDeclContextHelper helper(this, decl->staticDecls());
    while (tok != Token::EOS) {
        parseDeclaration();
    }
    return decl;
}

void Parser::parseDeclaration() {
    if (tok != Token::Word) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }

    if (tokWord() == words["has"]) {
        parseHasMemberDecl();
        return;
    }
    if (tokWord() == words["namespace"]) {
        parseNamespaceDecl();
        return;
    }

    ParameterDeclContextHelper helper(this);
    if (tokWord() == words["template"]) {
        nextToken();
        if (tok != Token::LeftParen) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        parameterDeclContext->templateParameterCount = parseParameters(ParameterParseOptions::OnlyInParameters);
        if (tok != Token::Word) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
    }

    if (tokWord() == words["fn"]) {
        parseFunctionDecl(std::move(helper));
        return;
    }
    if (tokWord() == words["struct"] || tokWord() == words["object"]) {
        parseTypeDecl(std::move(helper));
        return;
    }

    std::vector<WordAndLocation> attributes;
    while (tok == Token::Word) {
        attributes.push_back(tokWord());
        nextToken();
    }
    if (attributes.empty()) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    WordAndLocation name = attributes.back();
    attributes.pop_back();

    parseVariableDecl(name, attributes, std::move(helper));
}

void Parser::parseNamespaceDecl() {
    VERIFY(tok == Token::Word && tokWord() == words["namespace"]);
    nextToken();

    if (tok != Token::Word) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    WordAndLocation name = tokWord();
    nextToken();

    if (tok != Token::Colon) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    nextToken();

    if (tok != Token::LeftBrace) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    nextToken();

    auto prevDecls = staticDeclContext->decls(name);
    NamespaceDecl* decl;
    if (prevDecls.empty()) {
        decl = emitDecl<NamespaceDecl>(name);
    } else {
        Decl* prevDecl = *prevDecls.begin();
        if (prevDecl->kind() != DeclKind::Namespace) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        decl = (NamespaceDecl*)prevDecl;
    }
    StaticDeclContextHelper helper(this, decl->staticDecls());

    while (tok != Token::RightBrace) {
        parseDeclaration();
    }
    VERIFY(tok == Token::RightBrace);
    nextToken();
}

void Parser::parseTypeDecl(ParameterDeclContextHelper parameterHelper) {
    VERIFY(tok == Token::Word);
    DeclKind kind;
    if (tokWord() == words["struct"])
        kind = DeclKind::StructType;
    else if (tokWord() == words["object"])
        kind = DeclKind::ObjectType;
    else
        VERIFY_NOT_REACHED();
    nextToken();

    if (tok != Token::Word) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    WordAndLocation name = tokWord();
    nextToken();

    if (tok != Token::Colon) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    nextToken();

    if (tok != Token::LeftBrace) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    nextToken();

    TypeDecl* decl = emitDecl<TypeDecl>(kind, name, parameterHelper.get());
    StaticDeclContextHelper staticHelper(this, decl->staticDecls());
    while (tok != Token::RightBrace) {
        parseDeclaration();
    }
    VERIFY(tok == Token::RightBrace);
    nextToken();
}

void Parser::parseFunctionDecl(ParameterDeclContextHelper helper) {
    VERIFY(tok == Token::Word && tokWord() == words["fn"]);
    nextToken();

    if (tok != Token::Word) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    WordAndLocation name = tokWord();
    nextToken();

    VERIFY(tok == Token::LeftParen);
    parseParameters();

    Node* returnType = nextNodeLocation();
    if (tok == Token::Arrow) {
        nextToken();
        parseExpression();
    }
    emitNode(EmptyNode());

    Node* body = nextNodeLocation();
    parseBodyExprOrStmt();

    emitDecl<FunctionDecl>(name, helper.popContext(), returnType, body);
}

void Parser::parseVariableDecl(WordAndLocation name, std::span<const WordAndLocation> attributes, ParameterDeclContextHelper helper) {
    // static [var|let] name [: type] [= init];
    if (attributes.empty() || attributes.front() != words["static"]) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    DeclKind kind = DeclKind::StaticLetVariable;
    if (attributes.size() > 1) {
        if (attributes.size() > 2) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        if (attributes[1] == words["let"]) {
            kind = DeclKind::StaticLetVariable;
        } else if (attributes[1] == words["var"]) {
            kind = DeclKind::StaticVarVariable;
        } else {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
    }

    Node* typeExpr = nextNodeLocation();
    if (tok == Token::Colon) {
        nextToken();
        parseExpression();
    }
    emitNode(EmptyNode());

    Node* initExpr = nextNodeLocation();
    if (tok == Token::Equal) {
        nextToken();
        parseExpression();
    }
    emitNode(EmptyNode());

    if (tok != Token::SemiColon) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    nextToken();

    emitDecl<StaticVariableDecl>(kind, name, helper.popContext(), typeExpr, initExpr);
}

void Parser::parseHasMemberDecl() {
    VERIFY(tok == Token::Word && tokWord() == words["has"]);
    nextToken();

    Node* typeExpr = nextNodeLocation();
    parseBinaryOperatorExpr();
    emitNode(EmptyNode());

    Node* initExpr = nextNodeLocation();
    emitNode(EmptyNode());

    WordAndLocation name = {};
    if (tok == Token::Word && tokWord() == words["as"]) {
        nextToken();
        if (tok != Token::Word) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        name = tokWord();
        nextToken();
    }

    HasMemberDecl* decl = emitDecl<HasMemberDecl>(name, typeExpr, initExpr);
    if (tok == Token::Colon) {
        StaticDeclContextHelper helper(this, decl->staticDecls());
        nextToken();
        if (tok != Token::LeftBrace) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        nextToken();
        while (tok != Token::RightBrace) {
            parseDeclaration();
        }
        VERIFY(tok == Token::RightBrace);
        nextToken();
    } else if (tok == Token::SemiColon) {
        nextToken();
    } else {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
}

int_t Parser::parseParameters(ParameterParseOptions opts) {
    VERIFY(tok == Token::LeftParen);
    nextToken();
    int_t count = 0;
    while (tok != Token::RightParen) {
        // [let|var|inout|out] name [?constrait] [: type] [= init]
        if (tok != Token::Word) {
            errorHandler->expectedParameterName(this);
            VERIFY(tok == Token::Word);
        }

        DeclKind kind = DeclKind::LetParameter;
        WordAndLocation name = tokWord();
        nextToken();
        if (tok == Token::Word) {
            if (opts == ParameterParseOptions::OnlyInParameters) {
                errorHandler->parameterModifierNotAllowed(this, name, tokWord());
            }
            if (name == words["let"]) {
                kind = DeclKind::LetParameter;
            } else if (name == words["var"]) {
                kind = DeclKind::VarParameter;
            } else if (name == words["in"]) {
                kind = DeclKind::InParameter;
            } else if (name == words["inout"]) {
                kind = DeclKind::InOutParameter;
            } else if (name == words["out"]) {
                kind = DeclKind::OutParameter;
            } else {
                errorHandler->invalidParameterModifier(this, name, tokWord());
            }
            name = tokWord();
            nextToken();
        }

        if (tok == Token::Question) {
            // TODO: parse constraints
            VERIFY_NOT_REACHED();
        }

        Node* typeExpr = nextNodeLocation();
        if (tok == Token::Colon) {
            nextToken();
            parseExpression();
        }
        emitNode(EmptyNode());

        Node* initExpr = nextNodeLocation();
        if (tok == Token::Equal) {
            nextToken();
            parseExpression();
        }
        emitNode(EmptyNode());

        emitDecl<ParameterOrMemberDecl>(kind, name, typeExpr, initExpr);
        count += 1;
        if (tok == Token::Comma) {
            nextToken();
        } else if (tok != Token::RightParen) {
            errorHandler->unexpectedAfterParameter(this, name);
        }
    }
    VERIFY(tok == Token::RightParen);
    nextToken();

    // TODO: we should guard against overflow somewhere
    return count;
}

void Parser::parseBodyExprOrStmt() {
    if (tok == Token::Colon) {
        parseSingleOrCompoundStmt();
    } else if (tok == Token::FatArrow) {
        nextToken();
        parseExpression();
        emitNode(EmptyNode());
        consumeSemiColon(this);
    } else {
        errorHandler->expectedFunctionBody(this);
    }
}

// statements
void Parser::parseSingleOrCompoundStmt() {
    VERIFY(tok == Token::Colon);
    nextToken();
    if (tok == Token::LeftBrace) {
        parseCompoundStmt();
    } else {
        parseStatement();
    }
}

void Parser::parseCompoundStmt() {
    VERIFY(tok == Token::LeftBrace);
    emitNode(CompoundStmt(tokRange()));
    nextToken();
    while (tok != Token::RightBrace) {
        parseStatement();
    }
    VERIFY(tok == Token::RightBrace);
    emitNode(EmptyNode(tokRange()));
    nextToken();
}

void Parser::parseStatement() {
    auto kind = parseStatementInternal();
    if (kind != ParsedStatementKind::Normal) {
        errorHandler->expectedSemiColon(this);
    }
}

static NodeKind updateToStmt(Token token) {
    static constexpr auto firstUpdateStmt
        = std::to_underlying(firstInstance<matchNodeType<UpdateStmt>>());
    return NodeKind(std::to_underlying(token) - std::to_underlying(Token::FirstUpdateOp) + firstUpdateStmt);
}
Parser::ParsedStatementKind Parser::parseStatementInternal() {
    if (tok == Token::Word) {
        if (tokWord() == words["if"]) {
            parseIfExprOrStmt(/* statement = */ true);
            return ParsedStatementKind::Normal;
        }
        if (tokWord() == words["return"]) {
            auto returnLoc = tokRange();
            nextToken();

            if (tok == Token::SemiColon) {
                emitNode(EmptyReturnStmt(returnLoc));
                nextToken();
                return ParsedStatementKind::Normal;
            }

            parseExpression();
            emitNode(ReturnStmt(returnLoc));

            consumeSemiColon(this);
            return ParsedStatementKind::Normal;
        }
        if (tokWord() == words["let"] || tokWord() == words["var"]) {
            DeclKind declKind;
            if (tokWord() == words["let"])
                declKind = DeclKind::BlockLetVariable;
            else
                declKind = DeclKind::BlockVarVariable;
            auto declaratorLoc = tokRange();
            nextToken();
            if (tok != Token::Word) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            WordAndLocation name = tokWord();
            nextToken();

            LetStmt* stmtNode = emitNode(LetStmt(declaratorLoc));

            Node* typeExpr = nextNodeLocation();
            if (tok == Token::Colon) {
                nextToken();
                parseExpression();
            }
            emitNode(EmptyNode());

            Node* initExpr = nextNodeLocation();
            if (tok == Token::Equal) {
                nextToken();
                parseExpression();
            }
            emitNode(EmptyNode());

            stmtNode->setDecl(emitDecl<BlockVariableDecl>(declKind, name, typeExpr, initExpr));

            consumeSemiColon(this);
            return ParsedStatementKind::Normal;
        }
    }
    parseBinaryOperatorExpr();
    if (isUpdateOp(tok)) {
        emitNode(UpdateStmt(updateToStmt(tok), tokRange()));
        nextToken();
        parseExpression();
        consumeSemiColon(this);
        return ParsedStatementKind::Normal;
    } else {
        parseCommaElseExprHere();
        emitNode(ExpressionStmt(tokRange()));
        if (tok == Token::SemiColon) {
            nextToken();
            return ParsedStatementKind::Normal;
        }
        return ParsedStatementKind::ExprStmtWithMissingSemiColon;
    }
}

void Parser::parseIfExprOrStmt(bool statement) {
    VERIFY(tok == Token::Word && tokWord() == words["if"]);
    auto ifLoc = tokRange();
    nextToken();
    parseCommaElseExpr();
    if (tok == Token::FatArrow) {
        emitNode(IfExpr(ifLoc));
        nextToken();
        parseBinaryOperatorExpr();
        if (statement) {
            parseCommaElseExprHere();
            emitNode(ExpressionStmt(tokRange()));
            consumeSemiColon(this);
        }
        return;
    }
    if (tok == Token::Colon && statement) {
        emitNode(IfStmt(ifLoc));
        parseSingleOrCompoundStmt();
        return;
    }
    errorHandler->expectedIfBody(this, statement);
}

// expressions
void Parser::parseExpression() {
    parseCommaElseExpr();
}

void Parser::parseCommaElseExpr() {
    parseIfExpr();
    parseCommaElseExprHere();
}
void Parser::parseCommaElseExprHere() {
    if (tok != Token::Comma)
        return;
    auto commaToken = fullToken();
    nextToken();
    if (tok != Token::Word || tokWord() != words["else"]) {
        reemitLastToken(commaToken);
        return;
    }
    auto elseLoc = tokRange();
    nextToken();
    if (tok == Token::FatArrow) {
        emitNode(CommaElseExpr(elseLoc));
        nextToken();
        parseIfExpr();
    } else {
        errorHandler->expectedElseBody(this);
    }
}

void Parser::parseIfExpr() {
    if (tok != Token::Word || tokWord() != words["if"]) {
        parseBinaryOperatorExpr();
        return;
    }
    parseIfExprOrStmt(/* statement = */ false);
}

static NodeKind binaryToExpr(Token tok) {
    static constexpr auto firstBinaryExpr
        = std::to_underlying(firstInstance<matchNodeType<BinaryOperatorExpr>>());
    return NodeKind(std::to_underlying(tok) - std::to_underlying(Token::FirstBinaryOp) + firstBinaryExpr);
}
void Parser::parseBinaryOperatorExpr() {
    parseUnaryOperatorExpr();

    // The precedence is resolved later by the NodeStreamVisitor.
    while (isBinaryOp(tok)) {
        emitNode(BinaryOperatorExpr(binaryToExpr(tok), tokRange()));
        nextToken();
        parseUnaryOperatorExpr();
    }
}

static NodeKind unaryToExpr(Token tok) {
    static constexpr auto firstUnaryExpr = std::to_underlying(firstInstance<matchNodeType<UnaryOperatorExpr>>());
    return NodeKind(std::to_underlying(tok) - std::to_underlying(Token::FirstUnaryOp) + firstUnaryExpr);
}
void Parser::parseUnaryOperatorExpr() {
    if (isUnaryOp(tok)) {
        UnaryOperatorExpr expr(unaryToExpr(tok), tokRange());
        nextToken();
        parseUnaryOperatorExpr();
        emitNode(expr);
    } else {
        parsePostfixExpr();
    }
}

void Parser::parsePostfixExpr() {
    parsePrimaryExpr();
    parsePostfixExprHere();
}
void Parser::parsePostfixExprHere() {
    for (;;) {
        if (tok == Token::Point || tok == Token::ColonColon) {
            NodeKind kind = tok == Token::Point ? NodeKind::MemberAccessExpr : NodeKind::StaticAccessExpr;
            auto location = tokRange();
            nextToken();
            if (tok != Token::Word) {
                errorHandler->expectedAccessExpr(this);
            }
            emitNode(AccessExpr(kind, location, tokWord()));
            nextToken();
            if (tok == Token::LeftBrace) {
                emitNode(Parameterize(tokRange()));
                parseArguments();
            }
        } else if (tok == Token::LeftParen || tok == Token::LeftSquare) {
            NodeKind kind = tok == Token::LeftParen ? NodeKind::CallExpr : NodeKind::IndexExpr;
            emitNode(CallExpr(kind, tokRange()));
            parseArguments();
        } else if (tok == Token::PlusPlus || tok == Token::MinusMinus) {
            NodeKind kind = tok == Token::PlusPlus ? NodeKind::PostIncrementExpr : NodeKind::PostDecrementExpr;
            emitNode(UnaryOperatorExpr(kind, tokRange()));
            nextToken();
        } else {
            break;
        }
    }
}

void Parser::parsePrimaryExpr() {
    if (tok == Token::Word) {
        emitNode(IdentifierExpr(tokRange(), tokWord()));
        nextToken();
        if (tok == Token::LeftBrace) {
            emitNode(Parameterize(tokRange()));
            parseArguments();
        }
    } else if (tok == Token::LeftParen) {
        emitNode(ParenthesizedExpr(tokRange()));
        parseArguments();
    } else if (tok == Token::LeftSquare) {
        emitNode(CompoundExpr(tokRange()));
        nextToken();
        for (;;) {
            auto kind = parseStatementInternal();
            if (kind == ParsedStatementKind::ExprStmtWithMissingSemiColon
                && tok == Token::RightSquare) {
                break;
            }
            if (kind == ParsedStatementKind::ExprStmtWithMissingSemiColon) {
                errorHandler->expectedSemiColon(this);
            }
        }
        VERIFY(tok == Token::RightSquare);
        emitNode(EmptyNode(tokRange()));
        nextToken();
    } else if (tok == Token::CharacterLiteral) {
        emitNode(CharacterLiteralExpr(tokRange()));
        nextToken();
    } else if (tok == Token::NumericLiteral) {
        emitNode(NumericLiteralExpr(tokRange()));
        nextToken();
    } else {
        errorHandler->expectedExpression(this);
    }
}

void Parser::parseArguments() {
    VERIFY(isLeftBracket(tok));
    Token rightBracket = leftToRightBracket(tok);
    nextToken();
    while (tok != rightBracket) {
        if (tok == Token::Word) {
            auto desToken = fullToken();
            auto des = DesignateArgument(tokRange(), tokWord());
            nextToken();
            if (tok == Token::Equal) {
                nextToken();
                parseExpression();
                emitNode(des);
            } else {
                reemitLastToken(desToken);
                parseExpression();
            }
        } else {
            parseExpression();
        }
        if (tok == Token::Comma) {
            nextToken();
        } else if (tok != rightBracket) {
            errorHandler->unexpectedAfterArgument(this);
        }
    }
    VERIFY(tok == rightBracket);
    emitNode(EmptyNode(tokRange()));
    nextToken();
}