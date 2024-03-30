#include <sema/Generator.h>

namespace sema {

void Generator::advance() { tok += 1; }

Expression Generator::topExpression(int_t n) {
    return &scratchBlock[(expressionStack.end() - n - 1)->nodeIndex];
}

void Generator::popExpression() {
    int_t size = scratchBlock.back().subTreeSize();
    scratchBlock.resize(scratchBlock.size() - size);
    expressionStack.pop_back();
}

void Generator::visitDeclaration() {
    if (tok->kind() == Token::TemplateAttribute) {
        visitTemplateParameters();
    }
    if (tok->kind() == Token::ObjectTypeDecl || tok->kind() == Token::StructTypeDecl) {
        // VERIFY_NOT_REACHED();
    } else if (tok->kind() == Token::StaticLetDecl || tok->kind() == Token::StaticVarDecl) {
        visitStaticVariableDeclaration();
    } else if (tok->kind() == Token::FunctionDecl) {
        VERIFY_NOT_REACHED();
    } else {
        VERIFY_NOT_REACHED();
    }
}

void Generator::visitTemplateParameters() {
    while (tok->kind() != Token::EmptyNode) {
        visitTemplateParameter();
    }
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
}

void Generator::visitPrimaryExpr() {
    if (tok->kind() == Token::IdentifierExpr) {
        generateIdentifierExpr();
        advance();
    } else {
        VERIFY_NOT_REACHED();
    }
}

}