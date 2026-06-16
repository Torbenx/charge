#include <server/Hover.h>

#include <sema/Formatter.h>
#include <server/json_objects.h>

namespace server {

struct VariableInfo {
    Word name;
    sema::Constant type;
    sema::VariableCategory category;
};

std::variant<std::monostate, sema::Constant, VariableInfo> extractStaticInfo(sema::Util& util, sema::Expression e) {
    if (e.isConstant())
        return e.constant();

    switch (e.kind()) {
    case sema::ExpressionKind::Call:
        return util.program->getCall(e).callTarget;
    case sema::ExpressionKind::GlobalReference$Program:
    case sema::ExpressionKind::GlobalReference$Parameterize:
        return e.referencedGlobal();
    case sema::ExpressionKind::TemplateParameterReference: {
        const auto& p = util.program->parameters[e.templateParameterIndex()];
        if (p.implicit())
            return {}; // How did we even get here?
        return VariableInfo {
            .name = p.name,
            .type = sema::Constant(p.type),
            .category = sema::VariableKind::Let
        };
    }
    case sema::ExpressionKind::ParameterReference: {
        auto* fnProg = sema::cast<sema::FunctionProgram>(util.program);
        const auto& p = fnProg->functionParameters[e.parameterIndex()];
        return VariableInfo { .name = p.name(), .type = p.type(), .category = p.category() };
    }
    case sema::ExpressionKind::ReferenceReference:
    case sema::ExpressionKind::VariableReference:
        // TODO: No access to metadata :(
        return {};
    case sema::ExpressionKind::MemberExpression:
        return util.program->getMemberExpression(e).memberPointer;
    default:
        return {};
    }
}

std::optional<Hover::ServerCaps> Hover::initialize(Server& server, const ClientCaps& clientCaps) {
    // Order of formats indicates client preference, use first known format.
    bool foundMatch = false;
    for (const auto& format : clientCaps.contentFormat.value_or(std::vector<std::string> {})) {
        if (format == lsp::MarkupKind::Markdown) {
            m_useMarkdown = true;
            foundMatch = true;
            break;
        } else if (format == lsp::MarkupKind::PlainText) {
            m_useMarkdown = false;
            foundMatch = true;
            break;
        }
    }
    if (!foundMatch) {
        server.error("Hover disabled because no valid content format was found");
        return std::nullopt;
    }

    // Successful initialize
    return ServerCaps {};
}

Hover::Result Hover::doRequest(Server& server, const Params& params) {
    auto& context = server.acquireContext(params.textDocument.path());
    auto location = server.fromLSP(context, params.position);
    auto tokHandle = context.containingIdentifier(location);
    if (!tokHandle.has_value())
        return {};
    auto token = context.tokenBuffer.token(tokHandle.value());
    auto util = context.utilFor(tokHandle.value());

    sema::Formatter formatter { util };
    if (parse::isExpression(token.kind()) || parse::isVariableDecl(token.kind())) {
        auto expr = token.data2<parse::DataKind::Expression>();
        if (expr.has_value()) {
            auto staticInfo = extractStaticInfo(util, expr.value());
            if (std::holds_alternative<sema::Constant>(staticInfo)) {
                sema::Constant c = std::get<sema::Constant>(staticInfo);
                if (!formatter.formatAsDeclaration(c))
                    formatter.formatConstant(c);
            } else if (std::holds_alternative<VariableInfo>(staticInfo)) {
                auto [name, type, category] = std::get<VariableInfo>(staticInfo);
                formatter.formatVariableDeclaration(name, type, category);
            }
        }
    } else if (token.kind() == parse::TokenKind::NamespaceDecl) {
        formatter.formatAsDeclaration(sema::Constant(token.data2<parse::DataKind::DeclarationValue>().nsHandle()));
    } else if (parse::isProgramDecl(token.kind())) {
        formatter.formatAsDeclaration(sema::Constant(util.program->selfConstant()));
    } else if (parse::isEnumValueDecl(token.kind())) {
        auto valueIndex = token.data2<parse::DataKind::DeclIndex>();
        formatter.formatEnumValueDeclaration(sema::Constant(util.program->selfConstant()), valueIndex);
    } else if (parse::isMemberDecl(token.kind())) {
        auto memberIndex = token.kind() == parse::TokenKind::BaseMemberDecl
            ? token.data1<parse::DataKind::DeclIndex>()
            : token.data2<parse::DataKind::DeclIndex>();
        formatter.formatMemberDeclaration(sema::Constant(util.program->selfConstant()), memberIndex);
    } else if (parse::isVariableDecl(token.kind())) {
        token.data2<parse::DataKind::Expression>();
    }

    if (formatter.output.empty())
        return {};

    HoverResult result;
    if (m_useMarkdown) {
        result.contents.kind = lsp::MarkupKind::Markdown;
        result.contents.value = std::format("```charge\n{}\n```", formatter.output);
    } else {
        result.contents.kind = lsp::MarkupKind::PlainText;
        result.contents.value = std::move(formatter.output);
    }
    return { result };
}

void Hover::handleRequest(Server& server, RequestHandle handle, const Params& params) {
    server.completeRequest(handle, doRequest(server, params));
}

}