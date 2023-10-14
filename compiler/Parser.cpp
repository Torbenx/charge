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

#define NODE(kind, type) \
    case NodeKind::kind: \
        return #kind;
#include "nodes.h"

    case NodeKind::COUNT:
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
};
template<std::derived_from<Decl> T, typename... Args>
T* Parser::emitDecl(Args&&... args) {
    auto declOffset = nodeStream.offset;
    T* decl = std::construct_at(nodeStream.template allocate<T>(), std::forward<Args>(args)...);

    static_assert(std::derived_from<T, StaticDecl> || std::derived_from<T, ParameterOrMemberDecl>);
    (std::derived_from<T, StaticDecl> ? staticDeclStack : parameterDeclStack).emit({ decl->name, declOffset });

    if (instrumenter)
        instrumenter->emitDecl(this, decl);
    return decl;
}

StaticDecl* Parser::parseModule() {
    DeclarationScope onlyTheModuleScope(this);
    {
        TemplatedDeclarationScope moduleScope(this);
        while (tok != Token::EOS) {
            parseDeclaration();
        }
        emitDecl<StaticDecl>(NodeKind::ModuleDecl, WordAndLocation(), moduleScope.finish());
    }
    auto decls = onlyTheModuleScope.finish();
    VERIFY(decls.parameterCount == 0);
    VERIFY(decls.staticCount == 1);
    Decl* moduleDecl = decls.statics()[0];
    VERIFY(moduleDecl->kind() == NodeKind::ModuleDecl);
    return (StaticDecl*)moduleDecl;
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
    emitNode(EndScope());

    Node* initExpr = nextNodeLocation();
    if (tok == Token::Equal) {
        nextToken();
        parseExpression();
    }
    emitNode(EndScope());

    if (tok != Token::SemiColon) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    nextToken();

    emitDecl<VariableOrFunctionDecl>(kind, name, templateScope.finish(), typeExpr, initExpr);
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
    emitNode(EndScope());

    Node* body = nextNodeLocation();
    parseBodyExprOrStmt();

    emitDecl<VariableOrFunctionDecl>(NodeKind::FunctionDecl, name, templateScope.finish(), returnType, body);
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
        emitNode(EndScope());

        Node* initExpr = nextNodeLocation();
        if (tok == Token::Equal) {
            nextToken();
            parseExpression();
        }
        emitNode(EndScope());

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
        emitNode(EndScope());
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
    emitNode(EndScope(tokRange()));
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
        = std::min(std::to_underlying(firstInstance<matchNodeType<UpdateStmt>>()),
            std::to_underlying(firstInstance<matchNodeType<LogicalUpdateStmt>>()));
    return NodeKind(std::to_underlying(token) - std::to_underlying(Token::FirstUpdateOp) + firstUpdateStmt);
}
Parser::ParsedStatementKind Parser::parseStatementInternal() {
    if (tok == Token::Word) {
        if (tokWord() == words["if"]) {
            parseIfExprOrStmt(/* statement = */ true);
            return ParsedStatementKind::Normal;
        }
    }
    parseBinaryOperatorExpr();
    if (isUpdateOp(tok)) {
        NodeKind kind = updateToStmt(tok);
        auto opLoc = tokRange();
        nextToken();
        if (matchNodeType<LogicalUpdateStmt>(kind)) {
            emitNode(LogicalUpdateStmt(kind, opLoc));
            parseExpression();
            emitNode(EndScope());
        } else {
            parseExpression();
            emitNode(UpdateStmt(kind, opLoc));
        }
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
        emitNode(EndScope());
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
        emitNode(EndScope());
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
        = std::min(std::to_underlying(firstInstance<matchNodeType<BinaryOperatorExpr>>()),
            std::to_underlying(firstInstance<matchNodeType<BinaryLogicalOperatorExpr>>()));
    return NodeKind(std::to_underlying(tok) - std::to_underlying(Token::FirstBinaryOp) + firstBinaryExpr);
}
// clang-format off
static int precedenceOf(NodeKind node) {
    using enum NodeKind;
    switch (node) {
    case MultiplyExpr: return 1;
    case DivideExpr: return 1;
    case RemainderExpr: return 1;
    case AdditionExpr: return 2;
    case SubtractionExpr: return 2;
    case ShiftLeftExpr: return 3;
    case ShiftRightExpr: return 3;
    case CompareLessExpr: return 4;
    case CompareLessEqualExpr: return 4;
    case CompareGreaterExpr: return 4;
    case CompareGreaterEqualExpr: return 4;
    case CompareNotEqualExpr: return 5;
    case CompareEqualExpr: return 5;
    case BitwiseAndExpr: return 6;
    case BitwiseXorExpr: return 7;
    case BitwiseOrExpr: return 8;
    case LogicalAndExpr: return 9;
    case LogicalOrExpr: return 10;
    default: VERIFY_NOT_REACHED();
    }
}
// clang-format on
void Parser::parseBinaryOperatorExpr(int ambientPrecedence) {
    parseUnaryOperatorExpr();

    while (isBinaryOp(tok) || isBinaryLogicOp(tok)) {
        NodeKind kind = binaryToExpr(tok);
        int tokPrecdence = precedenceOf(kind);
        if (tokPrecdence >= ambientPrecedence) {
            // the operator described by 'tok' will be evaluated later
            break;
        }

        // the operator described by 'tok' must be evaluated first
        auto opLoc = tokRange();
        nextToken();
        // parse operators with precedence < tokPrecedence
        if (matchNodeType<BinaryLogicalOperatorExpr>(kind)) {
            emitNode(BinaryLogicalOperatorExpr(kind, opLoc));
            parseBinaryOperatorExpr(tokPrecdence);
            emitNode(EndScope());
        } else {
            parseBinaryOperatorExpr(tokPrecdence);
            emitNode(BinaryOperatorExpr(kind, opLoc));
        }
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
        emitNode(EndScope(tokRange()));
        nextToken();
    } else if (tok == Token::CharacterLiteral) {
        VERIFY_NOT_REACHED();
    } else if (tok == Token::NumericLiteral) {
        VERIFY_NOT_REACHED();
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
    emitNode(EndScope(tokRange()));
    nextToken();
}