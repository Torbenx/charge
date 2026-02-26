#include <server/SemaContext.h>

#include <sema/Generator.h>

using namespace sema;

namespace server {

DeclarationInfo SemaUtil::extractDeclarationInfo(const parse::TokenInfo& token) {
    if (parse::lexerToken(token.kind()) != parse::LexerToken::Identifier)
        return {};

    if (token.kind() == parse::TokenKind::NamespaceDecl)
        // TODO: No access to metadata :(
        return {};
    if (parse::isProgramDecl(token.kind()))
        return programHandle;
    if (token.kind() == parse::TokenKind::MemberDecl)
        return MemberDeclaration { programHandle, token.data2<parse::DataKind::DeclIndex>() };
    if (parse::isEnumValueDecl(token.kind()))
        return EnumValueDeclaration { programHandle, token.data2<parse::DataKind::DeclIndex>() };
    if (parse::isVariableDecl(token.kind()))
        // TODO: No access to metadata :(
        return {};

    if (!parse::isExpression(token.kind()))
        return {};
    auto maybeExpr = token.data2<parse::DataKind::Expression>();
    if (!maybeExpr.has_value())
        return {};

    // There are 3 semantic expression tokens for identifier tokens:
    // IdentifierExpr, StaticAccessExpr and MemberAccessExpr
    // The possible expression data for each of them are:
    // IdentifierExpr  : Namespace, Program, Parameterize, GlobalReference, EnumValue,
    //                   TemplateParameterReference, VariableReference, ParameterReference, ReferenceReference
    // StaticAccessExpr: Namespace, Program, Parameterize, GlobalReference, MemberPointer, EnumValue
    // MemberAccessExpr: Program, Parameterize, MemberExpression
    Expression e = maybeExpr.value();
    if (!e.isConstant()) {
        switch (e.kind()) {
        case ExpressionKind::MemberExpression:
            e = program->getMemberExpression(e).memberPointer;
            break;
        case ExpressionKind::GlobalReference$Program:
        case ExpressionKind::GlobalReference$Parameterize:
            e = e.referencedGlobal();
            break;
        case ExpressionKind::TemplateParameterReference:
            return LocalDeclaration(LocalDeclarationKind::TemplateParameter, e.templateParameterIndex());
        case ExpressionKind::ParameterReference:
            return LocalDeclaration(LocalDeclarationKind::FunctionParameter, e.parameterIndex());
        case ExpressionKind::VariableReference:
        case ExpressionKind::ReferenceReference:
            // TODO: No access to metadata :(
            return {};
        default:
            return {};
        }
    }
    VERIFY(e.isConstant());
    Constant c = e.constant();
    if (c.isEnumValueLiteral()) {
        auto enumValue = program->getEnumValue(c);
        return EnumValueDeclaration { baseProgram(Constant(enumValue.enumType)).value(), enumValue.valueIndex };
    }
    switch (c.kind()) {
    case ConstantKind::Namespace:
        return c.nsHandle();
    case ConstantKind::Program:
        return c.program();
    case ConstantKind::Parameterize:
        return program->getParameterize(c).base;
    case ConstantKind::MemberPointer: {
        auto pointer = program->getMemberPointer(c);
        if (pointer.isIdentity())
            return {};
        auto lastLink = pointer[pointer.linkCount() - 1];
        return MemberDeclaration { baseProgram(lastLink.parentType).value(), lastLink.memberIndex };
    }
    default:
        return {};
    }
}

std::optional<SourceLocation> SemaUtil::declarationLocation(const DeclarationInfo& info) {
    return std::visit([&]<typename T>(T v) -> std::optional<SourceLocation> {
        if constexpr (std::is_same_v<T, NamespaceHandle>) {
            return std::nullopt;
        } else if constexpr (std::is_same_v<T, ProgramHandle>) {
            return context.program(v)->declarationLocation();
        } else if constexpr (std::is_same_v<T, MemberDeclaration>) {
            return cast<StructProgram>(context.program(v.structProgram))->members[v.memberIndex].location();
        } else if constexpr (std::is_same_v<T, EnumValueDeclaration>) {
            return cast<EnumProgram>(context.program(v.enumProgram))->values[v.valueIndex].location();
        } else if constexpr (std::is_same_v<T, LocalDeclaration>) {
            if (v.kind == LocalDeclarationKind::TemplateParameter) {
                return program->parameters[v.id].location;
            } else if (v.kind == LocalDeclarationKind::FunctionParameter) {
                return cast<FunctionProgram>(program)->functionParameters[v.id].location();
            }
            return std::nullopt;
        } else {
            return std::nullopt;
        }
    },
        info);
}

void SemaContext::signatureCheckAll() {
    for (auto handle : programsInModule(thisModule()))
        Generator::signatureCheck(*this, handle);
    m_scratchProgram = newProgram(ProgramKind::Struct, Word(), parse::TokenHandle(), globalNamespace(), SourceLocation());
}

void SemaContext::forEachTokenImpl(void* data, void (*f)(void*, SemaUtil&, parse::TokenHandle)) {
    for (int_t i = 0; i < tokenBuffer.tokens.size(); i++) {
        // TODO: This is not a very effecient implementation
        parse::TokenHandle tokHandle { (uint32_t)i };
        auto util = utilFor(tokHandle);
        f(data, util, tokHandle);
    }
}

}