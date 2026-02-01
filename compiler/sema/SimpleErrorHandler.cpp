#include <sema/SimpleErrorHandler.h>

#include <sema/Generator.h>
#include <sema/errors.h>
#include <cxxabi.h>

namespace sema {

std::string ErrorBase::name() const {
    size_t inOutBufferSize = 0;
    int outStatus = 0;
    char* nameBuf = abi::__cxa_demangle(mangledName(), nullptr, &inOutBufferSize, &outStatus);
    VERIFY(nameBuf != nullptr);

    std::string_view name(nameBuf);
    static constexpr std::string_view prefix = "sema::errors::";
    VERIFY(name.starts_with(prefix));
    name = name.substr(prefix.length());

    std::string result(name);
    std::free(nameBuf);
    return result;
}

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
    g.expressionToConstant(std::nullopt); // Required to drop instructions
    return g.takeTopExpression();
}

void ErrorAnalyzer::analyze(ErrorBase* base) {
    using namespace errors;

    if (auto* e = dynamic_cast<Error<SelfTypeTemplateParameterWithExplicitType>*>(base); e != nullptr) {
        // Recover by ignoring the type expression
        visitAndDropExpression();
    }
    if (auto* e = dynamic_cast<Error<SelfTypeTemplateParameterWithDefaultArgument>*>(base); e != nullptr) {
        // Recover by ignoring the default argument
        visitAndDropExpression();
    }

    if (auto* e = dynamic_cast<Error<StaticVariableDeclarationWithoutInitializer>*>(base); e != nullptr) {
        // Only for global lets is the initialzer used during analysis. Recover those by making them open.
        auto* prog = cast<GlobalProgram>(g.program);
        if (prog->globalKind() == GlobalKind::Let)
            prog->m_globalKind = GlobalKind::OpenLet;
    }

    if (auto* e = dynamic_cast<Error<FunctionImplFunctionParameterCountMismatch>*>(base); e != nullptr) {
        // No recovery needed?
    }
    if (auto* e = dynamic_cast<Error<FunctionImplFunctionParameterNameMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<FunctionImplFunctionParameterKindMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<FunctionImplFunctionParameterGenericCategoryMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<FunctionImplFunctionParameterTypeMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<FunctionImplReturnTypeMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }

    if (auto* e = dynamic_cast<Error<FunctionParameterWithDefaultArgument>*>(base); e != nullptr) {
        // Recover by ignoring the default argument
        dropTopExpression();
    }
    if (auto* e = dynamic_cast<Error<SelfFunctionParameterWithDefaultArgument>*>(base); e != nullptr) {
        // Recover by ignoring the default argument
        VERIFY(g.tok->kind() == Token::AssignStmt);
        g.advance();
        visitAndDropExpression();
    }
    if (auto* e = dynamic_cast<Error<SelfFunctionParameterWithExplicitType>*>(base); e != nullptr) {
        // Recover by ignoring the type expression
        visitAndDropExpression();
    }
    if (auto* e = dynamic_cast<Error<FunctionWithoutExplicitReturnType>*>(base); e != nullptr) {
        // TODO: How to recover?
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<DestoryTargetNotALocalVariable>*>(base); e != nullptr) {
        // Recover by eating the top expression. Nothing will be done in the generator.
        dropTopExpression();
    }
    if (auto* e = dynamic_cast<Error<DiscardTargetNotALocalReference>*>(base); e != nullptr) {
        // Recover by eating the top expression. Nothing will be done in the generator.
        dropTopExpression();
    }

    if (auto* e = dynamic_cast<Error<ImplicitImplTargetNotFound>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<ImplicitImplTargetNotAProgram>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<ImplicitImplTargetKindMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<ImplicitImplTemplateParameterCountMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<ImplicitImplTemplateParameterNameMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }
    if (auto* e = dynamic_cast<Error<ImplicitImplTemplateParameterTypeMismatch>*>(base); e != nullptr) {
        // No recovery needed
    }

    if (auto* e = dynamic_cast<Error<ParameterizeBaseIsNotATemplate>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<ParameterizeBaseIsAlreadyParameterized>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<ParameterizeBaseNotSupported>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<ParameterizeWithTooManyArguments>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<ParameterizeArgumentNameMismatch>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<ParameterizeOfGlobalIncomplete>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<CallTargetNotSupported>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<CallTargetTemplateArgumentDeductionIncomplete>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<UnqualifiedLookupFailed>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<UnqualifiedLookupFoundMember>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<StaticLookupFailed>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<StaticLookupBaseExpressionNotSupported>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<StaticLookupBaseConstantNotSupported>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<MemberLookupFailed>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<MemberLookupResultNotSupported>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<MemberLookupFunctionResultNotImmediatelyCalled>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<MemberFunctionCallTargetHasNoSelfParameter>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<MemberFunctionCallSelfParameterTypeMismatch>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<MemberFunctionCallTargetTemplateArgumentDeductionIncomplete>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<SelfParameterLookupFailed>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<SelfTypeTemplateParameterLookupFailed>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<InitializeTypeMismatch>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<InitializeOfReferenceWithValue>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
    if (auto* e = dynamic_cast<Error<InitializeOfReferenceIsNotReferenceDowncast>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<MemberDeclarationWithoutExplicitType>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }

    if (auto* e = dynamic_cast<Error<ExplicitImplExpressionNotSupported>*>(base); e != nullptr) {
        // Fatal error
        VERIFY_NOT_REACHED();
    }
}

void SimpleErrorHandler::handleError(Generator& g, ErrorBase& base) {
    ErrorAnalyzer a { g };
    a.analyze(&base);
}

}