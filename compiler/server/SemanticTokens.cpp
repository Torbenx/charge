#include <server/SemanticTokens.h>

#include <server/json_objects.h>

namespace server {

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokenTypes
std::string_view SemanticTokens::lspName(Token t) {
    switch (t) {
    case Token::Namespace:
        return "namespace";
    case Token::Type:
        return "type";
    case Token::Enum:
        return "enum";
    case Token::Struct:
        return "struct";
    case Token::TypeParameter:
        return "typeParameter";
    case Token::Parameter:
        return "parameter";
    case Token::Variable:
        return "variable";
    case Token::EnumMember:
        return "enumMember";
    case Token::Function:
        return "function";
    default:
        VERIFY_NOT_REACHED();
    }
}

std::optional<SemanticTokens::ServerCaps> SemanticTokens::initialize(Server&, const ClientCaps& clientCaps) {
    if (!std::ranges::contains(clientCaps.formats, std::string_view("relative")))
        return std::nullopt;

    std::vector<std::string> typeLegend;
    for (int_t i = 0; i < (int_t)Token::COUNT; i++) {
        auto name = lspName((Token)i);
        if (std::ranges::contains(clientCaps.tokenTypes, name)) {
            m_enableMask.set(i, true);
            typeLegend.emplace_back(name);
        } else {
            typeLegend.push_back({});
        }
    }
    std::vector<std::string> modLegend;
    m_hasStaticMod = std::ranges::contains(clientCaps.tokenModifiers, std::string_view("static"));
    if (m_hasStaticMod) {
        modLegend.push_back("static");
    }

    if (!enabled(Token::Enum))
        m_enumToken = Token::Type;
    if (!enabled(Token::Struct))
        m_structToken = Token::Type;
    if (!enabled(Token::TypeParameter))
        m_typeParameterToken = Token::Parameter;

    if (m_enableMask.none())
        return std::nullopt;

    return ServerCaps {
        .legend = { .tokenTypes = std::move(typeLegend), .tokenModifiers = std::move(modLegend) },
        .range = false,
        .full = true
    };
}

SemanticTokens::Result SemanticTokens::doRequest(Server& server, const Params& params) {
    auto& context = server.acquireContext(params.textDocument.path());
    lsp::Position prevPos { .line = 0, .character = 0 };
    std::vector<int32_t> output;
    context.forEachToken([&](SemaUtil util, parse::TokenHandle tokHandle) {
        auto token = context.tokenBuffer.token(tokHandle);
        auto declInfo = util.extractDeclarationInfo(token);
        struct Out {
            Token tok = Token::COUNT;
            bool staticMod = false;
        };
        auto out = std::visit([&]<typename T>(T v) -> Out {
            if constexpr (std::is_same_v<T, sema::NamespaceHandle>) {
                return { Token::Namespace, true };
            } else if constexpr (std::is_same_v<T, sema::ProgramHandle>) {
                auto* prog = context.program(v);
                switch (prog->kind()) {
                case sema::ProgramKind::Enum:
                    return { m_enumToken, true };
                case sema::ProgramKind::Function:
                    return { Token::Function, true };
                case sema::ProgramKind::Global:
                    return { cast<sema::GlobalProgram>(prog)->type() == sema::builtins::type_type ? Token::Type : Token::Variable, true };
                case sema::ProgramKind::Struct:
                    return { m_structToken, true };
                default:
                    VERIFY_NOT_REACHED();
                }
            } else if constexpr (std::is_same_v<T, MemberDeclaration>) {
                return { Token::Variable, false };
            } else if constexpr (std::is_same_v<T, EnumValueDeclaration>) {
                return { Token::EnumMember, true };
            } else if constexpr (std::is_same_v<T, LocalDeclaration>) {
                if (v.kind == LocalDeclarationKind::TemplateParameter) {
                    return { Token::Parameter, true };
                } else if (v.kind == LocalDeclarationKind::FunctionParameter) {
                    return { Token::Parameter, false };
                }
            }
            return Out();
        },
            declInfo);
        if (out.tok == Token::COUNT)
            return;
        lsp::Position pos = server.toLSP(context, token.location());
        int_t lineDiff = (int_t)pos.line - (int_t)prevPos.line;
        int_t offsetDiff = (int_t)pos.character - (lineDiff != 0 ? 0 : prevPos.character);
        prevPos = pos;
        VERIFY(lineDiff >= 0);
        VERIFY(offsetDiff >= 0);

        output.push_back(lineDiff);
        output.push_back(offsetDiff);
        output.push_back(context.tokenBuffer.tokenSpelling(token).length());
        output.push_back(std::to_underlying(out.tok));
        output.push_back(m_hasStaticMod && out.staticMod ? 1 : 0);
    });
    if (output.empty())
        return {};
    return { Tokens { .data = std::move(output) } };
}

void SemanticTokens::handleRequest(Server& server, RequestHandle handle, const Params& params) {
    server.completeRequest(handle, doRequest(server, params));
}

}