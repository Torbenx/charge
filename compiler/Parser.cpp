#include "compiler.h"

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
    T* node = std::construct_at(nodeAllocator.template allocate<T>(), in);
    if (instrumenter)
        instrumenter->emitNode(this, node);
    return node;
}
template<std::derived_from<Decl> T, typename... Args>
T* Parser::emitDecl(Args&&... args) {
    T* decl = std::construct_at(nodeAllocator.template allocate<T>(), std::forward<Args>(args)...);
    if (instrumenter)
        instrumenter->emitDecl(this, decl);
    return decl;
}

id<Decl> Parser::parseDeclaration() {
    if (tok == Token::Word && tokWord() == words["with"]) {
        nextToken();
        if (tok != Token::LeftParen) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        parseParameters(ParameterParseScope::Template);
    }
    if (tok == Token::Word && tokWord() == words["template"]) {
        nextToken();
        if (tok != Token::LeftParen) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        parseParameters(ParameterParseScope::Template);
    }

    std::vector<WordAndLocation> attributes;
    while (tok == Token::Word) {
        if (tokWord() == words["namespace"] || tokWord() == words["struct"] || tokWord() == words["object"])
            return parseNamespaceOrTypeDecl(attributes);
        if (tokWord() == words["fn"])
            return parseFunctionDecl(attributes);

        attributes.push_back(tokWord());
        nextToken();
    }

    if (attributes.empty()) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    auto name = attributes.back();
    attributes.pop_back();

    return parseVariableDecl(name, attributes);
}

id<Decl> Parser::parseNamespaceOrTypeDecl(std::span<const WordAndLocation> attributes) {
    VERIFY(tok == Token::Word);
    WordAndLocation declarator = tokWord();
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
    std::vector<std::pair<Word, StaticDecl*>> decls;
    while (tok != Token::RightBrace) {
        id<Decl> decl = parseDeclaration();
        decls.push_back({ decl->name, (StaticDecl*)(Decl*)decl });
    }
    VERIFY(tok == Token::RightBrace);
    nextToken();
    return emitDecl<StaticDecl>();
}

id<Decl> Parser::parseVariableDecl(WordAndLocation name, std::span<const WordAndLocation> attributes) {
    // static [mut] name [: type] [= init];
    Node* typeExpr = nullptr;
    Node* initExpr = nullptr;
    if (tok == Token::Colon) {
        nextToken();
        typeExpr = nextNodeLocation();
        parseExpression();
        emitNode(EndScope());
    }
    if (tok == Token::Equal) {
        nextToken();
        initExpr = nextNodeLocation();
        parseExpression();
        emitNode(EndScope());
    }
    if (tok != Token::SemiColon) {
        // errorHandler;
        VERIFY_NOT_REACHED();
    }
    return emitDecl<StaticDecl>();
}

id<Decl> Parser::parseFunctionDecl(std::span<const WordAndLocation> attributes) {
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
    parseParameters(ParameterParseScope::Function);
    VERIFY_NOT_REACHED();
}

static bool mutAllowed(Parser::ParameterParseScope s) {
    return s == Parser::ParameterParseScope::Function;
}
static bool ampAllowed(Parser::ParameterParseScope s) {
    return s == Parser::ParameterParseScope::Function;
}
void Parser::parseParameters(ParameterParseScope scope) {
    VERIFY(tok == Token::LeftParen);
    nextToken();
    while (tok != Token::RightParen) {
        // [mut] name[&] [?constrait] [: type] [= init]
        NodeKind kind = NodeKind::ValueParameterDecl;
        if (tok == Token::Word && tokWord() == words["mut"]) {
            if (!mutAllowed(scope)) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            kind = NodeKind::MutParameterDecl;
            nextToken();
        }

        if (tok != Token::Word) {
            // errorHandler;
            VERIFY_NOT_REACHED();
        }
        WordAndLocation name = tokWord();
        nextToken();

        if (tok == Token::Amp) {
            if (kind == NodeKind::MutParameterDecl) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            if (!ampAllowed(scope)) {
                // errorHandler;
                VERIFY_NOT_REACHED();
            }
            kind = NodeKind::AmpParameterDecl;
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
    return NodeKind(std::to_underlying(token) - std::to_underlying(Token::FirstUpdateOp)
        + std::to_underlying(NodeKind::FirstUpdateStmt));
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
        if (isLogicalUpdateStmt(kind)) {
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
    return NodeKind(std::to_underlying(tok) - std::to_underlying(Token::FirstBinaryOp)
        + std::to_underlying(NodeKind::FirstBinaryOperatorExpr));
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
        if (isBinaryLogicalOperatorExpr(kind)) {
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
    return NodeKind(std::to_underlying(tok) - std::to_underlying(Token::FirstUnaryOp)
        + std::to_underlying(NodeKind::FirstUnaryOperatorExpr));
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