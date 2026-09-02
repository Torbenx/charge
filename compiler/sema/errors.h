#pragma once

namespace sema::errors {

struct SelfTypeTemplateParameterWithExplicitType { };
struct SelfTypeTemplateParameterWithDefaultArgument { };

struct StaticVariableDeclarationWithoutInitializer { };

struct FunctionImplFunctionParameterCountMismatch { };
struct FunctionImplFunctionParameterNameMismatch { };
struct FunctionImplFunctionParameterKindMismatch { };
struct FunctionImplFunctionParameterGenericCategoryMismatch { };
struct FunctionImplFunctionParameterTypeMismatch { };
struct FunctionImplReturnTypeMismatch { };

struct StructImplToFewMembers { };
struct StructImplMemberIsBaseMismatch { };
struct StructImplMemberNameMismatch { };
struct StructImplMemberTypeMismatch { };

struct GlobalImplTargetNotOpen { };
struct GlobalImplTypeMismatch { };

struct FunctionParameterWithDefaultArgument { };
struct SelfFunctionParameterWithDefaultArgument { };
struct SelfFunctionParameterWithExplicitType { };
struct FunctionWithoutExplicitReturnType { };
struct OpenReturnTypeOnNonTemplateFunction { };
struct ReferenceReturnTypeNotSupported { };

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
struct ParameterizeIncomplete { };

struct CallTargetNotSupported { };
struct CallTargetTemplateArgumentDeductionIncomplete { };

struct UnqualifiedLookupFoundMember { };
struct UnqualifiedLookupFailed { };

struct StaticLookupFailed { };
struct StaticLookupBaseConstantNotSupported { };
struct StaticLookupBaseExpressionNotSupported { };

struct MemberLookupFailed { };
struct MemberLookupResultNotSupported { };
struct MemberLookupFunctionResultNotImmediatelyCalled { };

struct MemberFunctionCallTargetHasNoSelfParameter { };
struct MemberFunctionCallSelfParameterTypeMismatch { };
struct MemberFunctionCallTargetTemplateArgumentDeductionIncomplete { };

struct SelfParameterLookupFailed { };
struct SelfTypeTemplateParameterLookupFailed { };

struct InitializeTypeMismatch { };
struct InitializeOfReferenceWithValue { };
struct InitializeOfReferenceIsNotReferenceDowncast { };

struct MemberDeclarationWithoutExplicitType { };

struct ExplicitImplExpressionNotSupported { };

}