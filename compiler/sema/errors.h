#pragma once

namespace sema::errors {

struct SelfTypeTemplateParameterWithExplicitType { };
struct SelfTypeTemplateParameterWithDefaultArgument { };

struct StaticVariableDeclarationWithoutInitializer { };

struct FunctionImplFunctionParameterCountMismatch { };
struct FunctionImplFunctionParameterNameMismatch { };
struct FunctionImplFunctionParameterKindMismatch { };
struct FunctionImplFunctionParameterCategoryMismatch { };
struct FunctionImplFunctionParameterTypeMismatch { };
struct FunctionImplReturnTypeMismatch { };

struct FunctionParameterWithDefaultArgument { };
struct SelfFunctionParameterWithDefaultArgument { };
struct SelfFunctionParameterWithExplicitType { };
struct FunctionWithoutExplicitReturnType { };

struct DestoryTargetNotALocalVariable { };
struct DiscardTargetNotALocalReference { };

struct ImplicitImplTargetNotFound { };
struct ImplicitImplTargetNotAProgram { };
struct ImplicitImplTargetKindMismatch { };
struct ImplicitImplTemplateParameterCountMismatch { };
struct ImplicitImplTemplateParameterNameMismatch { };
struct ImplicitImplTemplateParameterTypeMismatch { };

struct ParameterizeBaseIsNotATemplate { };
struct ParameterizeBaseIsAlreadyParameterized { };
struct ParameterizeBaseNotSupported { };
struct ParameterizeWithTooManyArguments { };
struct ParameterizeArgumentNameMismatch { };

}