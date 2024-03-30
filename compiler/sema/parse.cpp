#include <sema/Generator.h>

namespace sema {

void Generator::visitTemplateParameter() {
    if (tok->kind() != Token::ImplicitKindParameter) {
        // report error
        VERIFY_NOT_REACHED();
    }
    Word name = Word::fromUint(tok->data());
    advance();

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

    program->addExplicitParameter(name, type, initializer);
}

void Generator::advance() { tok += 1; }

void Generator::visitDeclaration() {
    if (tok->kind() == Token::TemplateAttribute) {
        visitTemplateParameters();
    }
}

void Generator::visitTemplateParameters() {
    while (tok->kind() != Token::EmptyNode) {
        visitTemplateParameter();
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