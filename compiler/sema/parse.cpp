#include <glue/Context.h>
#include <sema/Generator.h>

namespace sema {

Generator::Generator(glue::Context& context, ProgramHandle handle)
    : context(context), program(&context.programs[handle.id()]), programHandle(handle) { }

Generator::Generator(glue::Context& context, glue::DeclarationNode* scope)
    : context(context) {
    currentScope = scope;
    if (scope->parseLocation().has_value())
        tok = &context.parseOutput.tokens[scope->parseLocation().value().id()];
    programHandle = scope->program().value();
    program = &context.programs[programHandle.id()];
}

void Generator::advance() { tok += 1; }

Expression Generator::topExpression(int_t n) {
    return &nodeScratch[(nodeStack.end() - n - 1)->nodeIndex];
}

void Generator::popExpression() {
    int_t size = nodeScratch.back().subTreeSize();
    for (int_t i = 0; i < size; i++)
        nodeScratch.pop_back();
    nodeStack.pop_back();
}

void Generator::popExpressions(int_t n) {
    for (int_t i = 0; i < n; i++)
        popExpression();
}

void Generator::visitDeclaration() {
    if (tok->kind() == Token::TemplateAttribute) {
        visitTemplateParameters();
    }
    if (tok->kind() == Token::ObjectTypeDecl || tok->kind() == Token::StructTypeDecl) {
        // VERIFY_NOT_REACHED();
        program->setType(builtins::type_type);
    } else if (tok->kind() == Token::StaticLetDecl || tok->kind() == Token::StaticVarDecl) {
        visitStaticVariableDeclaration();
    } else if (tok->kind() == Token::FunctionDecl) {
        visitFunctionDeclaration();
    } else {
        VERIFY_NOT_REACHED();
    }
}

void Generator::visitTemplateParameters() {
    VERIFY(program->parameters.size() == program->inheritedParameterCount);
    advance();
    while (tok->kind() != Token::EmptyNode) {
        Program::Parameter explicitParameter = visitTemplateParameter();
        VERIFY(!explicitParameter.name.empty());

        program->parameters.push_back(explicitParameter);
        parameterTypes.push_back(verifyType((Value)explicitParameter.type));
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();
}

Generator::VariableDeclaration Generator::visitVariableDeclaration(bool programParameters) {
    Type type;
    int_t oldParameterCount = parameterTypes.size();
    if (programParameters)
        VERIFY(oldParameterCount == (int_t)program->parameters.size());
    if (tok->kind() != Token::AssignStmt) {
        // parse type
        wildcardMeaning = WildcardMeaning::ImplicitTemplate;
        visitExpression();
        implicitToType();
        type = verifyType(makeExpressionValue());
        wildcardMeaning = WildcardMeaning::Error;
    } else {
        type = verifyType(newImplicitParameter(builtins::type_type));
    }
    VERIFY(tok->kind() == Token::AssignStmt);
    advance();

    bool hasInitializer = false;
    if (tok->kind() != Token::ExpressionStmt) {
        if (oldParameterCount != (int_t)parameterTypes.size() && programParameters) {
            // 'type' contains implicitly created parameters.
            // Converting the initializer to 'type' can never happend without them.
            VERIFY_NOT_REACHED();
        }
        visitExpression();

        DeductionState state(program, parameterTypes.size());
        state.identityMap(oldParameterCount);
        implicitCastTo(state, type);
        VERIFY(state.isComplete());
        type = verifyType(fold(FoldState { program, INVALID_VALUE, state.arguments }, type));

        hasInitializer = true;
    }
    VERIFY(tok->kind() == Token::ExpressionStmt);
    advance();

    if (programParameters) {
        // add implicit parameters to program
        while (program->parameters.size() < parameterTypes.size())
            program->parameters.push_back({ Word(), parameterTypes[program->parameters.size()], std::nullopt });
    }

    return { type, hasInitializer };
}

Program::Parameter Generator::visitTemplateParameter() {
    if (tok->kind() != Token::ImplicitKindParameter) {
        // report error
        VERIFY_NOT_REACHED();
    }
    Word name = Word::fromUint(tok->data());
    advance();

    auto info = visitVariableDeclaration(true);
    std::optional<Value> initializer;
    if (info.hasInitializer)
        initializer = makeExpressionValue();
    return { name, info.type, initializer };
}

void Generator::visitStaticVariableDeclaration() {
    VERIFY(tok->kind() == Token::StaticLetDecl || tok->kind() == Token::StaticVarDecl);
    bool isVar = tok->kind() == Token::StaticVarDecl;
    advance();

    auto info = visitVariableDeclaration(false);
    VERIFY(info.hasInitializer);
    program->setType(info.type);
    program->setValue(makeExpressionValue());
}

void Generator::visitFunctionDeclaration() {
    VERIFY(tok->kind() == Token::FunctionDecl);
    advance();

    int_t firstStackEntry = nodeStack.size();

    while (tok->kind() != Token::EmptyNode) {
        VERIFY(tok->kind() == Token::ImplicitKindParameter);
        Word name = Word::fromUint(tok->data());
        auto nameLoc = tok->location();
        advance();
        auto info = visitVariableDeclaration(true);
        emitNode(NodeKind::LetDecl, nameLoc, info.hasInitializer ? 1 : 0, NodeData { .decl { .type = info.type } });
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();

    if (tok->kind() == Token::BodyExpr) {
        advance();
        visitExpression();
        program->setType(topExpression().type());
        VERIFY(tok->kind() == Token::ExpressionStmt);
        emitNode(NodeKind::ExpressionStmt, tok->location(), 1, NodeData { .empty {} });
    } else {
        std::optional<Type> type;
        if (tok->kind() == Token::ReturnType) {
            advance();
            visitExpression();
            type = verifyType(makeExpressionValue());
        }
        VERIFY(tok->kind() == Token::FunctionBody);
        // ... visit statement
        VERIFY_NOT_REACHED();
    }
}

void Generator::visitExpression() {
    visitBinaryExpr();
}

void Generator::visitBinaryExpr() {
    visitUnaryExpr();
}

void Generator::visitUnaryExpr() {
    if (parse::isUnaryExpr(tok->kind())) {
        // create placeholder node for the function
        advance();
        visitUnaryExpr();
        // resolve and create call expression
    } else {
        visitPostfixExpr();
    }
}

void Generator::visitPostfixExpr() {
    visitPrimaryExpr();
    if (tok->kind() == Token::Parameterize) {
        generateParameterizeExpr(visitExpressionList());
    }
}

void Generator::visitPrimaryExpr() {
    if (tok->kind() == Token::IdentifierExpr) {
        generateIdentifierExpr();
        advance();
    } else {
        VERIFY_NOT_REACHED();
    }
}

int_t Generator::visitExpressionList() {
    advance();
    int_t argumentCount = 0;
    while (tok->kind() != Token::EmptyNode) {
        argumentCount += 1;
        visitExpression();
    }
    advance();
    return argumentCount;
}

}