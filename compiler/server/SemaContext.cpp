#include <server/SemaContext.h>

#include <sema/Generator.h>
#include <server/Server.h>

#include <gtest/gtest.h>

using namespace sema;

namespace server {

DeclarationInfo SemaUtil::extractDeclarationInfo(const parse::TokenInfo& token) {
    if (parse::lexerToken(token.kind()) != parse::LexerToken::Identifier)
        return {};

    if (token.kind() == parse::TokenKind::NamespaceDecl)
        return token.data2<parse::DataKind::DeclarationValue>().nsHandle();
    if (parse::isProgramDecl(token.kind()))
        return programHandle;
    if (token.kind() == parse::TokenKind::MemberDecl)
        return MemberDeclaration { programHandle, token.data2<parse::DataKind::DeclIndex>() };
    if (parse::isEnumValueDecl(token.kind()))
        return EnumValueDeclaration { programHandle, token.data2<parse::DataKind::DeclIndex>() };

    // There are 3 semantic expression tokens for identifier tokens:
    // IdentifierExpr, StaticAccessExpr and MemberAccessExpr
    // The possible expression data for each of them are:
    // IdentifierExpr  : Namespace, Program, Parameterize, GlobalReference, EnumValue,
    //                   TemplateParameterReference, VariableReference, ParameterReference, ReferenceReference
    // StaticAccessExpr: Namespace, Program, Parameterize, GlobalReference, MemberPointer, EnumValue
    // MemberAccessExpr: Program, Parameterize, MemberExpression
    if (!parse::isExpression(token.kind()) && !parse::isVariableDecl(token.kind()))
        return {};
    auto maybeExpr = token.data2<parse::DataKind::Expression>();
    if (!maybeExpr.has_value())
        return {};
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
        case ExpressionKind::TemplateParameterReference: {
            ProgramHandle progHandle = programHandle;
            Program* prog = program;
            while (e.templateParameterIndex() < (int_t)prog->inheritedParameterCount) {
                VERIFY(prog->parent().kind() == DeclarationValueKind::Program);
                progHandle = prog->parent().program();
                prog = context.program(progHandle);
            }
            return LocalDeclaration(LocalDeclarationKind::TemplateParameter, e.templateParameterIndex(), progHandle);
        }
        case ExpressionKind::ParameterReference:
            return LocalDeclaration(LocalDeclarationKind::FunctionParameter, e.parameterIndex(), programHandle);
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
    makeScratchProgram();
}

void SemaContext::makeScratchProgram() {
    VERIFY(tokenBuffer.tokens.back().kind() == parse::TokenKind::EOS);
    parse::TokenHandle endHandle { uint32_t(tokenBuffer.tokens.size()) };
    m_scratchProgram = newProgram(ProgramKind::Struct, Word(), endHandle, globalNamespace(), SourceLocation());
    program(m_scratchProgram.value())->tokenRangeEnd = endHandle;
}

std::optional<parse::TokenHandle> SemaContext::containingIdentifier(SourceLocation location) {
    for (auto handle : tokenBuffer.findContainingTokens(location)) {
        if (parse::lexerToken(tokenBuffer.token(handle).kind()) == parse::LexerToken::Identifier)
            return handle;
    }
    return std::nullopt;
}

void SemaContext::forEachTokenImpl(const void* data, void (*f)(const void*, SemaUtil&, parse::TokenHandle)) {
    ProgramUnion* scratch = modules.back().programStorage + m_scratchProgram.value().id();
    ProgramUnion* cur = scratch;
    ProgramUnion* next = programStorage.begin();
    parse::TokenHandle tok { 0 };
    std::vector<ProgramUnion*> stack;
    for (;;) {
        SemaUtil util { *this, ProgramHandle(cur - modules.back().programStorage) };
        if (next->get().tokenRangeBegin < cur->get().tokenRangeEnd) {
            VERIFY(next->get().tokenRangeEnd <= cur->get().tokenRangeEnd);
            auto nextBegin = next->get().tokenRangeBegin;
            for (; tok < nextBegin; ++tok)
                f(data, util, tok);

            stack.push_back(cur);
            cur = next;
            next += 1;
            VERIFY(tok == cur->get().tokenRangeBegin);
        } else {
            auto rangeEnd = cur->get().tokenRangeEnd;
            for (; tok < rangeEnd; ++tok)
                f(data, util, tok);

            while (tok == cur->get().tokenRangeEnd) {
                cur = stack.back();
                stack.pop_back();
            }
            VERIFY(cur == scratch || cur->get().tokenRange().contains(tok));
        }
        if (next == scratch && cur == scratch)
            break;
    }
    VERIFY((int_t)tok.id() == tokenBuffer.tokens.size() - 1);
}

TEST(Server, ForEachToken) {
    std::filesystem::path testDir { COMPILER_TEST_DIR };
    for (auto fileName : { "files/declarations.chrg", "files/expressions.chrg" }) {
        auto filePath = testDir / fileName;
        auto fileSource = readFile(filePath);
        // No module dependency need since no semantic analysis will be done
        SemaContext context({}, fileSource);
        parse::Parser parser(fileSource.data());
        parser.parse(context);
        ASSERT_TRUE(parser.done());
        context.makeScratchProgram();
        context.forEachToken([&](SemaUtil& util, parse::TokenHandle handle) {
            EXPECT_EQ(context.utilFor(handle).programHandle, util.programHandle);
        });
    }
}

}