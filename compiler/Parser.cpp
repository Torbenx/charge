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

template<std::derived_from<Node> T>
T* Parser::emitNode(T in) {
    T* node = std::construct_at(nodeStream.template allocate<T>(), in);

    if (instrumenter)
        instrumenter->emitNode(this, node);
    return node;
}

struct DeclarationScopeFields {
    Parser* parser = nullptr;
    uint32_t parameterDeclStackBegin = 0;
    uint32_t staticDeclStackBegin = 0;
};
struct Parser::DeclarationScope : DeclarationScopeFields {
    explicit DeclarationScope(Parser* parser)
        : DeclarationScopeFields {
            .parser = parser,
            .parameterDeclStackBegin = (uint32_t)parser->parameterDeclStack.size(),
            .staticDeclStackBegin = (uint32_t)parser->staticDeclStack.size(),
        } { }

    DeclarationScope(DeclarationScope&& other)
        : DeclarationScopeFields(other) {
        (DeclarationScopeFields&)other = {};
    }

    DeclArrays finish() {
        node_stream_offset arrayBeginOffset = parser->nodeStream.offset;
        auto stackToArrayItem = [&](DeclStackItem item) -> DeclArrayItem {
            return { item.name, arrayBeginOffset - item.nodeStreamOffset };
        };

        auto parameterDecls = std::span(parser->parameterDeclStack).subspan(parameterDeclStackBegin);
        for (auto item : parameterDecls)
            std::construct_at(parser->nodeStream.allocate<DeclArrayItem>(), stackToArrayItem(item));

        auto staticDecls = std::span(parser->staticDeclStack).subspan(staticDeclStackBegin);
        for (auto item : staticDecls)
            std::construct_at(parser->nodeStream.allocate<DeclArrayItem>(), stackToArrayItem(item));

        parser->staticDeclStack.truncate(staticDeclStackBegin);
        parser->parameterDeclStack.truncate(parameterDeclStackBegin);

        DeclArrays arrays = {
            .begin = (DeclArrayItem*)parser->nodeStream.position(arrayBeginOffset),
            .parameterCount = (uint32_t)parameterDecls.size(),
            .staticCount = (uint32_t)staticDecls.size()
        };

        parser = nullptr;
        return arrays;
    }

    ~DeclarationScope() {
        if (parser) {
            parser->staticDeclStack.truncate(staticDeclStackBegin);
            parser->parameterDeclStack.truncate(parameterDeclStackBegin);
        }
    }
};
struct Parser::TemplatedDeclarationScope : Parser::DeclarationScope {
    uint32_t withParamCount = 0;
    uint32_t templateParamCount = 0;
    using DeclarationScope::DeclarationScope;

    TemplatedDeclArrays finish() {
        auto arrays = DeclarationScope::finish();
        return { arrays, withParamCount, templateParamCount };
    }

    void discard() {
        VERIFY(withParamCount == 0 && templateParamCount == 0);
        parser = nullptr;
    }
};
template<std::derived_from<Decl> T, typename... Args>
T* Parser::emitDecl(Args&&... args) {
    T* decl = std::construct_at(nodeStream.template allocate<T>(), std::forward<Args>(args)...);

    if constexpr (std::derived_from<T, StaticDecl> || std::derived_from<T, ParameterOrMemberDecl>) {
        (std::derived_from<T, StaticDecl> ? staticDeclStack : parameterDeclStack).emit({ decl->name, nodeStream.offsetOf(decl) });
    }

    if (instrumenter)
        instrumenter->emitDecl(this, decl);
    return decl;
}

// declarations
ModuleDecl* Parser::parseModule() {
    DeclarationScope onlyTheModuleScope(this);
    {
        TemplatedDeclarationScope moduleScope(this);
        while (tok != Token::EOS) {
            parseDeclaration();
        }
        emitDecl<ModuleDecl>(moduleScope.finish());
    }
    auto decls = onlyTheModuleScope.finish();
    VERIFY(decls.parameterCount == 0);
    VERIFY(decls.staticCount == 1);
    Decl* moduleDecl = decls.statics()[0];
    VERIFY(moduleDecl->kind() == NodeKind::ModuleDecl);
    return (ModuleDecl*)moduleDecl;
}

void Parser::parseDeclaration() {
    TemplatedDeclarationScope templateScope(this);
    if (tok == Token::Word && tokWord() == words["with"]) {
        nextToken();
        if (tok != Token::LeftParen) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        templateScope.withParamCount = parseParameters(ParameterParseOptions::OnlyLetParameters);
    }
    if (tok == Token::Word && tokWord() == words["template"]) {
        nextToken();
        if (tok != Token::LeftParen) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        templateScope.templateParamCount = parseParameters(ParameterParseOptions::OnlyLetParameters);
    }

    std::vector<WordAndLocation> attributes;
    while (tok == Token::Word) {
        if (tokWord() == words["namespace"] || tokWord() == words["struct"] || tokWord() == words["object"]) {
            parseNamespaceOrTypeDecl(attributes, std::move(templateScope));
            return;
        }
        if (tokWord() == words["fn"]) {
            parseFunctionDecl(attributes, std::move(templateScope));
            return;
        }
        if (tokWord() == words["has"]) {
            parseHasMemberDecl(attributes, std::move(templateScope));
            return;
        }

        attributes.push_back(tokWord());
        nextToken();
    }

    if (attributes.empty()) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    auto name = attributes.back();
    attributes.pop_back();

    parseVariableDecl(name, attributes, std::move(templateScope));
}

void Parser::parseNamespaceOrTypeDecl(std::span<const WordAndLocation> attributes, TemplatedDeclarationScope templateScope) {
    if (attributes.size() != 0) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }

    VERIFY(tok == Token::Word);
    WordAndLocation declarator = tokWord();
    NodeKind kind;
    if (declarator == words["namespace"])
        kind = NodeKind::NamespaceDecl;
    else if (declarator == words["struct"])
        kind = NodeKind::StructTypeDecl;
    else if (declarator == words["object"])
        kind = NodeKind::ObjectTypeDecl;
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

    while (tok != Token::RightBrace) {
        parseDeclaration();
    }
    VERIFY(tok == Token::RightBrace);
    nextToken();

    emitDecl<StaticDecl>(kind, name, templateScope.finish());
}

void Parser::parseVariableDecl(WordAndLocation name, std::span<const WordAndLocation> attributes, TemplatedDeclarationScope templateScope) {
    // static [mut|let] name [: type] [= init];
    if (attributes.empty() || attributes.front() != words["static"]) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    NodeKind kind = NodeKind::StaticLetVariableDecl;
    if (attributes.size() > 1) {
        if (attributes.size() > 2) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        if (attributes[1] == words["let"]) {
            kind = NodeKind::StaticLetVariableDecl;
        } else if (attributes[1] == words["mut"]) {
            kind = NodeKind::StaticMutVariableDecl;
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

    emitDecl<StaticVariableDecl>(kind, name, templateScope.finish(), typeExpr, initExpr);
}

void Parser::parseFunctionDecl(std::span<const WordAndLocation> attributes, TemplatedDeclarationScope templateScope) {
    if (attributes.size() != 0) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }

    VERIFY(tok == Token::Word && tokWord() == words["fn"]);
    nextToken();
    if (tok != Token::Word) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    WordAndLocation name = tokWord();
    nextToken();

    if (tok != Token::LeftParen) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    parseParameters();

    Node* returnType = nextNodeLocation();
    if (tok == Token::Arrow) {
        nextToken();
        parseExpression();
    }
    emitNode(EmptyNode());

    Node* body = nextNodeLocation();
    parseBodyExprOrStmt();

    emitDecl<FunctionDecl>(name, templateScope.finish(), returnType, body);
}

void Parser::parseHasMemberDecl(std::span<const WordAndLocation> attributes, TemplatedDeclarationScope templateScope) {
    templateScope.discard();
    if (attributes.size() != 0) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }

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

    DeclarationScope scope(this);
    if (tok == Token::Colon) {
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

    emitDecl<HasMemberDecl>(name, typeExpr, initExpr, scope.finish());
}

int_t Parser::parseParameters(ParameterParseOptions opts) {
    VERIFY(tok == Token::LeftParen);
    nextToken();
    int_t count = 0;
    while (tok != Token::RightParen) {
        // [let|mut|inout|out] name [?constrait] [: type] [= init]
        if (tok != Token::Word) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }

        NodeKind kind = NodeKind::LetParameterDecl;
        WordAndLocation name = tokWord();
        nextToken();
        if (tok == Token::Word) {
            if (opts == ParameterParseOptions::OnlyLetParameters) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            if (name == words["let"]) {
                kind = NodeKind::LetParameterDecl;
            } else if (name == words["mut"]) {
                kind = NodeKind::MutParameterDecl;
            } else if (name == words["inout"]) {
                kind = NodeKind::InOutParameterDecl;
            } else if (name == words["out"]) {
                kind = NodeKind::OutParameterDecl;
            } else {
                // errorHandler;
                VERIFY_NOT_REACHED();
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
            // errorHandler;
            VERIFY_NOT_REACHED();
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
        if (tok != Token::SemiColon) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        nextToken();
    } else {
        // errorHandler;
        VERIFY_NOT_REACHED();
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
    if (tok != Token::LeftBrace) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
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
        // errorHandler;
        VERIFY_NOT_REACHED();
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

            if (tok != Token::SemiColon) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            nextToken();
            return ParsedStatementKind::Normal;
        }
        if (tokWord() == words["let"] || tokWord() == words["mut"]) {
            NodeKind stmtKind = NodeKind::LetStmt;
            NodeKind declKind = NodeKind::BlockLetDecl;
            if (tokWord() == words["let"])
                nextToken();
            if (tok != Token::Word) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            if (tokWord() == words["mut"]) {
                stmtKind = NodeKind::MutLetStmt;
                declKind = NodeKind::BlockMutDecl;
                nextToken();
            }
            if (tok != Token::Word) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            WordAndLocation name = tokWord();
            nextToken();

            LetStmt* stmtNode = emitNode(LetStmt(stmtKind, name));

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

            stmtNode->setDecl(emitDecl<BlockLetDecl>(declKind, name, typeExpr, initExpr));

            if (tok != Token::SemiColon) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            nextToken();

            return ParsedStatementKind::Normal;
        }
    }
    parseBinaryOperatorExpr();
    if (isUpdateOp(tok)) {
        emitNode(UpdateStmt(updateToStmt(tok), tokRange()));
        nextToken();
        parseExpression();
        if (tok != Token::SemiColon) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        nextToken();
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
            if (tok != Token::SemiColon) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            nextToken();
        }
    } else if (tok == Token::Colon) {
        if (!statement) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        emitNode(IfStmt(ifLoc));
        parseSingleOrCompoundStmt();
    } else {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
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
        // errorHandler;
        VERIFY_NOT_REACHED();
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
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            emitNode(AccessExpr(kind, location, tokWord()));
            nextToken();
            if (tok == Token::LeftBrace) {
                emitNode(Parameterize(tokRange()));
                parseArguments();
            }
        } else if (tok == Token::LeftParen || tok == Token::LeftAngle) {
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
    } else if (tok == Token::LeftAngle) {
        emitNode(CompoundExpr(tokRange()));
        nextToken();
        for (;;) {
            auto kind = parseStatementInternal();
            if (kind == ParsedStatementKind::ExprStmtWithMissingSemiColon
                && tok == Token::RightAngle) {
                break;
            }
            if (kind == ParsedStatementKind::ExprStmtWithMissingSemiColon) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
        }
        VERIFY(tok == Token::RightAngle);
        emitNode(EmptyNode(tokRange()));
        nextToken();
    } else if (tok == Token::CharacterLiteral) {
        emitNode(CharacterLiteralExpr(tokRange()));
        nextToken();
    } else if (tok == Token::NumericLiteral) {
        emitNode(NumericLiteralExpr(tokRange()));
        nextToken();
    } else {
        // errorHandler;
        VERIFY_NOT_REACHED();
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
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
    }
    VERIFY(tok == rightBracket);
    emitNode(EmptyNode(tokRange()));
    nextToken();
}