#include <sema/SimpleErrorHandler.h>

#include <sema/Generator.h>
#include <sema/errors.h>

namespace sema {

struct ErrorAnalyzer {
    using Token = parse::TokenKind;
    Generator& g;

    Expression visitAndDropExpression();
    Expression dropTopExpression();
    void analyze(ErrorBase* base);
};

Expression ErrorAnalyzer::visitAndDropExpression() {
    g.visitExpression();
    return dropTopExpression();
}

Expression ErrorAnalyzer::dropTopExpression() {
    g.expressionToConstant(); // Required to drop instructions
    return g.takeTopExpression();
}

void ErrorAnalyzer::analyze(ErrorBase* base) {
    using namespace errors;

    if (auto* e = dynamic_cast<SelfTypeTemplateParameterWithExplicitType*>(base); e != nullptr) {
        // Recover by ignoring the type expression
        visitAndDropExpression();
    }
    if (auto* e = dynamic_cast<SelfTypeTemplateParameterWithDefaultArgument*>(base); e != nullptr) {
        // Recover by ignoring the default argument
        visitAndDropExpression();
    }

    if (auto* e = dynamic_cast<StaticVariableDeclarationWithoutInitializer*>(base); e != nullptr) {
        // Only for global lets is the initialzer used during analysis. Recover those by making them open.
        auto* prog = cast<GlobalProgram>(g.program);
        if (prog->globalKind() == GlobalKind::Let)
            prog->m_globalKind = GlobalKind::OpenLet;
    }

    if (auto* e = dynamic_cast<FunctionImplFunctionParameterCountMismatch*>(base); e != nullptr) {
        // No recovery needed?
    }
    if (auto* e = dynamic_cast<FunctionImplFunctionParameterNameMismatch*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<FunctionImplFunctionParameterKindMismatch*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<FunctionImplFunctionParameterCategoryMismatch*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<FunctionImplFunctionParameterTypeMismatch*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<FunctionImplReturnTypeMismatch*>(base); e != nullptr) {
        // No recovery needed
    }

    if (auto* e = dynamic_cast<FunctionParameterWithDefaultArgument*>(base); e != nullptr) {
        // Recover by ignoring the default argument
        dropTopExpression();
    }
    if (auto* e = dynamic_cast<SelfFunctionParameterWithDefaultArgument*>(base); e != nullptr) {
        // Recover by ignoring the default argument
        visitAndDropExpression();
    }
    if (auto* e = dynamic_cast<SelfFunctionParameterWithExplicitType*>(base); e != nullptr) {
        // Recover by ignoring the type expression
        visitAndDropExpression();
    }
    if (auto* e = dynamic_cast<FunctionWithoutExplicitReturnType*>(base); e != nullptr) {
        // TODO: How to recover?
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<DestoryTargetNotALocalVariable*>(base); e != nullptr) {
        // Recover by eating the top expression. Nothing will be done in the generator.
        dropTopExpression();
    }
    if (auto* e = dynamic_cast<DiscardTargetNotALocalReference*>(base); e != nullptr) {
        // Recover by eating the top expression. Nothing will be done in the generator.
        dropTopExpression();
    }
}

void SimpleErrorHandler::handleError(Generator& g, ErrorBase& base) {
    ErrorAnalyzer a { g };
    a.analyze(&base);
}

}