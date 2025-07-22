#pragma once

namespace sema {

struct ErrorBase {
    virtual ~ErrorBase() = default;
};

}

namespace sema::errors {

struct SelfTypeTemplateParameterWithExplicitType : ErrorBase { };
struct SelfTypeTemplateParameterWithDefaultArgument : ErrorBase { };

struct StaticVariableDeclarationWithoutInitializer : ErrorBase { };

struct FunctionImplFunctionParameterCountMismatch : ErrorBase { };
struct FunctionImplFunctionParameterNameMismatch : ErrorBase { };
struct FunctionImplFunctionParameterKindMismatch : ErrorBase { };
struct FunctionImplFunctionParameterCategoryMismatch : ErrorBase { };
struct FunctionImplFunctionParameterTypeMismatch : ErrorBase { };
struct FunctionImplReturnTypeMismatch : ErrorBase { };

struct FunctionParameterWithDefaultArgument : ErrorBase { };
struct SelfFunctionParameterWithDefaultArgument : ErrorBase { };
struct SelfFunctionParameterWithExplicitType : ErrorBase { };
struct FunctionWithoutExplicitReturnType : ErrorBase { };

struct DestoryTargetNotALocalVariable : ErrorBase { };
struct DiscardTargetNotALocalReference : ErrorBase { };

struct ImplicitImplTargetNotFound : ErrorBase { };
struct ImplicitImplTargetNotAProgram : ErrorBase { };
struct ImplicitImplTargetKindMismatch : ErrorBase { };
struct ImplicitImplTemplateParameterCountMismatch : ErrorBase { };
struct ImplicitImplTemplateParameterNameMismatch : ErrorBase { };
struct ImplicitImplTemplateParameterTypeMismatch : ErrorBase { };

struct ParameterizeBaseIsNotATemplate : ErrorBase { };
struct ParameterizeBaseIsAlreadyParameterized : ErrorBase { };
struct ParameterizeBaseNotSupported : ErrorBase { };
struct ParameterizeWithTooManyArguments : ErrorBase { };
struct ParameterizeArgumentNameMismatch : ErrorBase { };

}