#include <glue/Context.h>
#include <sema/Generator.h>

namespace sema {

Generator::Generator(glue::Context& context, Program* program)
    : context(context), program(program) { }

Generator::Generator(glue::Context& context, glue::DeclarationNode* scope)
    : context(context) {
    currentScope = scope;
    if (scope->parseLocation().has_value())
        tok = &context.parseOutput.tokens[scope->parseLocation().value().id()];
    program = &context.programs[scope->program().value().id()];
}

void Generator::advance() { tok += 1; }

Expression Generator::topExpression(int_t n) {
    return &expressionScratch[(expressionStack.end() - n - 1)->nodeIndex];
}

void Generator::popExpression() {
    int_t size = expressionScratch.back().subTreeSize();
    expressionScratch.resize(expressionScratch.size() - size);
    expressionStack.pop_back();
}

void Generator::popExpressions(int_t n) {
    for (int_t i = 0; i < n; i++)
        popExpression();
}

void Generator::visitDeclaration() {
    program->setStatus(ProgramStatus::SignatureCheckInProgress);
    if (tok->kind() == Token::TemplateAttribute) {
        visitTemplateParameters();
    }
    if (tok->kind() == Token::ObjectTypeDecl || tok->kind() == Token::StructTypeDecl) {
        // VERIFY_NOT_REACHED();
        program->setType(builtins::type_type);
    } else if (tok->kind() == Token::StaticLetDecl || tok->kind() == Token::StaticVarDecl) {
        visitStaticVariableDeclaration();
    } else if (tok->kind() == Token::FunctionDecl) {
        VERIFY_NOT_REACHED();
    } else {
        VERIFY_NOT_REACHED();
    }
    program->setStatus(ProgramStatus::SignatureChecked);
}

void Generator::visitTemplateParameters() {
    advance();
    while (tok->kind() != Token::EmptyNode) {
        visitTemplateParameter();
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();
}

Generator::VariableDeclaration Generator::visitVariableDeclaration() {
    Type type;
    if (tok->kind() != Token::AssignStmt) {
        // parse type
        wildcardMeaning = WildcardMeaning::ImplicitTemplate;
        visitExpression();
        implicitToType();
        type = verifyType(makeExpressionValue());
        wildcardMeaning = WildcardMeaning::Error;
    } else {
        type = verifyType(program->addImplicitParameter(builtins::type_type));
    }
    VERIFY(tok->kind() == Token::AssignStmt);
    advance();

    std::optional<Value> initializer;
    if (tok->kind() != Token::ExpressionStmt) {
        // parse initializer
        visitExpression();
        implicitCastTo(type);
        initializer = makeExpressionValue();
    }
    VERIFY(tok->kind() == Token::ExpressionStmt);
    advance();

    return { type, initializer };
}

void Generator::visitTemplateParameter() {
    if (tok->kind() != Token::ImplicitKindParameter) {
        // report error
        VERIFY_NOT_REACHED();
    }
    Word name = Word::fromUint(tok->data());
    advance();

    auto info = visitVariableDeclaration();
    program->addExplicitParameter(name, info.type, info.initializer);
}

void Generator::visitStaticVariableDeclaration() {
    VERIFY(tok->kind() == Token::StaticLetDecl || tok->kind() == Token::StaticVarDecl);
    bool isVar = tok->kind() == Token::StaticVarDecl;
    advance();

    auto info = visitVariableDeclaration();
    VERIFY(info.initializer.has_value());
    program->setType(info.type);
    program->setValue(info.initializer.value());
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