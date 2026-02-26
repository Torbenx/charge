#include <server/Server.h>

#include <parse/parse_impl.h>
#include <sema/Formatter.h>
#include <sema/Generator.h>
#include <server/json_objects.h>
#include <server/json_tuple.h>

#include <bitset>
#include <filesystem>
#include <fstream>

namespace server {

// ----------------------------- Helpers ----------------------------

std::filesystem::path uriToPath(std::string_view uri) {
    static constexpr std::string_view prefix = "file:///";
    VERIFY(uri.starts_with(prefix)); // TODO: Should not verify on user data
    return uri_decode(uri.substr(prefix.length()));
}

std::string pathToUri(const std::filesystem::path& path) {
    return "file:///" + uri_encode(path.string());
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream stream;
    stream.open(file, std::ios::binary);
    VERIFY(stream.good()); // TODO: Should not verify on user data
    stream.seekg(0, std::ios::end);
    int_t length = stream.tellg();
    VERIFY(length >= 0); // TODO: Should not verify on user data
    std::string sourceBuffer;
    sourceBuffer.resize(length);
    stream.seekg(0, std::ios::beg);
    stream.read(sourceBuffer.data(), length);
    stream.close();
    VERIFY(stream.good()); // TODO: Should not verify on user data

    return sourceBuffer;
}

// ------------------------------ Hover -----------------------------

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_hover
struct Hover : Server::Method {
    static constexpr FixedString clientCapName = "hover";
    static constexpr FixedString serverCapName = "hoverProvider";
    static constexpr FixedString method = "textDocument/hover";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#hoverClientCapabilities
    struct ClientCaps {
        JSON_OBJECT
        std::optional<std::vector<std::string>> JSON_MEMBER(contentFormat);
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#hoverOptions
    struct ServerCaps {
        JSON_OBJECT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#hoverParams
    using Params = lsp::TextDocumentPositionParams;

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#hover
    struct HoverResult {
        JSON_OBJECT
        lsp::MarkupContent JSON_MEMBER(contents);
        std::optional<lsp::Range> JSON_MEMBER(range);
    };
    using Result = json::Nullable<HoverResult>;

    std::optional<ServerCaps> initialize(Server& server, const ClientCaps& clientCaps) {
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

    Result doRequest(Server& server, const Params& params) {
        auto& context = server.acquireContext(params.textDocument.path());
        auto location = server.fromLSP(context, params.position);
        auto tokHandle = context.tokenBuffer.findContainingToken(location);
        if (!tokHandle.has_value())
            return {};
        auto token = context.tokenBuffer.token(tokHandle.value());
        sema::Util util = server.utilFor(context, tokHandle.value());

        sema::Formatter formatter { util };
        if (token.hasData2(parse::DataKind::Expression)) {
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
            // TODO: No access to metadata :(
        } else if (parse::isProgramDecl(token.kind())) {
            formatter.formatAsDeclaration(sema::Constant(util.program->selfConstant()));
        } else if (parse::isEnumValueDecl(token.kind())) {
            auto valueIndex = token.data2<parse::DataKind::DeclIndex>();
            formatter.formatEnumValueDeclaration(sema::Constant(util.program->selfConstant()), valueIndex);
        } else if (parse::isMemberDecl(token.kind())) {
            auto memberIndex = token.kind() == parse::TokenKind::HasMemberDecl
                ? token.data1<parse::DataKind::DeclIndex>()
                : token.data2<parse::DataKind::DeclIndex>();
            formatter.formatMemberDeclaration(sema::Constant(util.program->selfConstant()), memberIndex);
        } else if (parse::isVariableDecl(token.kind())) {
            // TODO: No access to metadata :(
        }

        if (formatter.output.empty())
            return {};

        HoverResult result;
        if (m_useMarkdown) {
            result.contents.kind = lsp::MarkupKind::Markdown;
            result.contents.value = fmt::format("```charge\n{}\n```", formatter.output);
        } else {
            result.contents.kind = lsp::MarkupKind::PlainText;
            result.contents.value = std::move(formatter.output);
        }
        return { result };
    }

    void handleRequest(Server& server, RequestHandle handle, const Params& params) {
        server.completeRequest(handle, doRequest(server, params));
    }

    bool m_useMarkdown = false;
};

enum class LocalDeclarationKind : uint8_t {
    TemplateParameter,
    FunctionParameter,
    Variable,
    Reference,
};
struct LocalDeclaration {
    LocalDeclarationKind kind;
    uint32_t id;
};
struct MemberDeclaration {
    sema::ProgramHandle structProgram;
    uint32_t memberIndex;
};
struct EnumValueDeclaration {
    sema::ProgramHandle enumProgram;
    uint32_t valueIndex;
};
using DeclarationInfo = std::variant<std::monostate, sema::NamespaceHandle, sema::ProgramHandle, MemberDeclaration, EnumValueDeclaration, LocalDeclaration>;

DeclarationInfo extractDeclarationInfo(sema::Util& util, const parse::TokenInfo& token) {
    if (token.kind() == parse::TokenKind::NamespaceDecl)
        // TODO: No access to metadata :(
        return {};
    if (parse::isProgramDecl(token.kind()))
        return util.programHandle;
    if (token.kind() == parse::TokenKind::MemberDecl)
        return MemberDeclaration { util.programHandle, token.data2<parse::DataKind::DeclIndex>() };
    if (parse::isEnumValueDecl(token.kind()))
        return EnumValueDeclaration { util.programHandle, token.data2<parse::DataKind::DeclIndex>() };
    if (parse::isVariableDecl(token.kind()))
        // TODO: No access to metadata :(
        return {};

    if (!parse::isExpression(token.kind()))
        return {};
    if (parse::lexerToken(token.kind()) != parse::LexerToken::Identifier)
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
    sema::Expression e = maybeExpr.value();
    if (!e.isConstant()) {
        switch (e.kind()) {
        case sema::ExpressionKind::MemberExpression:
            e = util.program->getMemberExpression(e).memberPointer;
            break;
        case sema::ExpressionKind::GlobalReference$Program:
        case sema::ExpressionKind::GlobalReference$Parameterize:
            e = e.referencedGlobal();
            break;
        case sema::ExpressionKind::TemplateParameterReference:
            return LocalDeclaration(LocalDeclarationKind::TemplateParameter, e.templateParameterIndex());
        case sema::ExpressionKind::ParameterReference:
            return LocalDeclaration(LocalDeclarationKind::FunctionParameter, e.parameterIndex());
        case sema::ExpressionKind::VariableReference:
        case sema::ExpressionKind::ReferenceReference:
            // TODO: No access to metadata :(
            return {};
        default:
            return {};
        }
    }
    VERIFY(e.isConstant());
    sema::Constant c = e.constant();
    if (c.isEnumValueLiteral()) {
        auto enumValue = util.program->getEnumValue(c);
        return EnumValueDeclaration { util.baseProgram(sema::Constant(enumValue.enumType)).value(), enumValue.valueIndex };
    }
    switch (c.kind()) {
    case sema::ConstantKind::Namespace:
        return c.nsHandle();
    case sema::ConstantKind::Program:
        return c.program();
    case sema::ConstantKind::Parameterize:
        return util.program->getParameterize(c).base;
    case sema::ConstantKind::MemberPointer: {
        auto pointer = util.program->getMemberPointer(c);
        if (pointer.isIdentity())
            return {};
        auto lastLink = pointer[pointer.linkCount() - 1];
        return MemberDeclaration { util.baseProgram(lastLink.parentType).value(), lastLink.memberIndex };
    }
    default:
        return {};
    }
}

std::optional<SourceLocation> getDeclarationLocation(sema::Util& util, const DeclarationInfo& info) {
    return std::visit([&]<typename T>(T v) -> std::optional<SourceLocation> {
        if constexpr (std::is_same_v<T, sema::NamespaceHandle>) {
            return std::nullopt;
        } else if constexpr (std::is_same_v<T, sema::ProgramHandle>) {
            return util.context.program(v)->declarationLocation();
        } else if constexpr (std::is_same_v<T, MemberDeclaration>) {
            return sema::cast<sema::StructProgram>(util.context.program(v.structProgram))->members[v.memberIndex].location();
        } else if constexpr (std::is_same_v<T, EnumValueDeclaration>) {
            return sema::cast<sema::EnumProgram>(util.context.program(v.enumProgram))->values[v.valueIndex].location();
        } else if constexpr (std::is_same_v<T, LocalDeclaration>) {
            if (v.kind == LocalDeclarationKind::TemplateParameter) {
                return util.program->parameters[v.id].location;
            } else if (v.kind == LocalDeclarationKind::FunctionParameter) {
                return sema::cast<sema::FunctionProgram>(util.program)->functionParameters[v.id].location();
            }
            return std::nullopt;
        } else {
            return std::nullopt;
        }
    },
        info);
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_declaration
struct GoToDeclaration : Server::Method {
    static constexpr FixedString method = "textDocument/declaration";
    static constexpr FixedString clientCapName = "declaration";
    static constexpr FixedString serverCapName = "declarationProvider";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#declarationClientCapabilities
    struct ClientCaps {
        JSON_OBJECT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#declarationOptions
    struct ServerCaps {
        JSON_OBJECT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#declarationParams
    using Params = lsp::TextDocumentPositionParams;

    using Result = json::Nullable<lsp::Location>;

    std::optional<ServerCaps> initialize(Server&, const ClientCaps&) { return ServerCaps(); }

    Result doRequest(Server& server, const Params& params) {
        auto& context = server.acquireContext(params.textDocument.path());
        auto location = server.fromLSP(context, params.position);
        auto tokHandle = context.tokenBuffer.findContainingToken(location);
        if (!tokHandle.has_value())
            return {};
        auto token = context.tokenBuffer.token(tokHandle.value());
        auto util = server.utilFor(context, tokHandle.value());

        auto declInfo = extractDeclarationInfo(util, token);
        auto maybeLoc = getDeclarationLocation(util, declInfo);
        if (!maybeLoc.has_value())
            return {};

        {
            SourceLocation location = maybeLoc.value();
            auto tokHandle = context.tokenBuffer.findContainingToken(location);
            VERIFY(tokHandle.has_value());
            int_t length = context.tokenBuffer.tokenSpelling(context.tokenBuffer.token(tokHandle.value())).length();
            lsp::Position pos = server.toLSP(context, location);
            lsp::Range range { pos, lsp::Position { .line = pos.line, .character = int32_t(pos.character + length) } };
            return { lsp::Location {
                .uri = params.textDocument.uri,
                .range = range,
            } };
        }
    }

    void handleRequest(Server& server, RequestHandle handle, const Params& params) {
        server.completeRequest(handle, doRequest(server, params));
    }
};

struct SemanticTokens : Server::Method {
    static constexpr FixedString method = "textDocument/semanticTokens/full";
    static constexpr FixedString clientCapName = "semanticTokens";
    static constexpr FixedString serverCapName = "semanticTokensProvider";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokensLegend
    struct Legend {
        JSON_OBJECT
        std::vector<std::string> JSON_MEMBER(tokenTypes);
        std::vector<std::string> JSON_MEMBER(tokenModifiers);
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokensClientCapabilities
    struct ClientCaps {
        JSON_OBJECT
        std::vector<std::string> JSON_MEMBER(tokenTypes);
        std::vector<std::string> JSON_MEMBER(tokenModifiers);
        std::vector<std::string> JSON_MEMBER(formats);
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokensOptions
    struct ServerCaps {
        JSON_OBJECT
        Legend JSON_MEMBER(legend);
        std::optional<bool> JSON_MEMBER(range);
        std::optional<bool> JSON_MEMBER(full);
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokensParams
    struct Params {
        JSON_OBJECT
        lsp::TextDocumentIdentifier JSON_MEMBER(textDocument);
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokens
    struct Tokens {
        JSON_OBJECT
        std::vector<int32_t> JSON_MEMBER(data);
    };

    using Result = json::Nullable<Tokens>;

    enum class Token : uint8_t {
        Namespace,
        Type,
        Enum,
        Struct,
        TypeParameter,
        Parameter,
        Variable,
        EnumMember,
        Function,

        COUNT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokenTypes
    static std::string_view lspName(Token t) {
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

    std::optional<ServerCaps> initialize(Server&, const ClientCaps& clientCaps) {
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

    Result doRequest(Server& server, const Params& params) {
        sema::Context& context = server.acquireContext(params.textDocument.path());
        lsp::Position prevPos { .line = 0, .character = 0 };
        std::vector<int32_t> output;
        server.forEachToken(context, [&](sema::Util util, parse::TokenHandle tokHandle) {
            auto token = context.tokenBuffer.token(tokHandle);
            auto declInfo = extractDeclarationInfo(util, token);
            struct Out {
                Token tok = Token::COUNT;
                bool staticMod = false;
            };
            auto out = std::visit([&]<typename T>(T v) -> Out {
                if constexpr (std::is_same_v<T, sema::NamespaceHandle>) {
                    return { Token::Namespace, true };
                } else if constexpr (std::is_same_v<T, sema::ProgramHandle>) {
                    switch (context.program(v)->kind()) {
                    case sema::ProgramKind::Enum:
                        return { m_enumToken, true };
                    case sema::ProgramKind::Function:
                        return { Token::Function, true };
                    case sema::ProgramKind::Global:
                        return { Token::Variable, true };
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

    void handleRequest(Server& server, RequestHandle handle, const Params& params) {
        server.completeRequest(handle, doRequest(server, params));
    }

    bool enabled(Token t) const { return m_enableMask[std::to_underlying(t)]; }

    std::bitset<std::to_underlying(Token::COUNT)> m_enableMask;
    Token m_enumToken = Token::Enum;
    Token m_structToken = Token::Struct;
    Token m_typeParameterToken = Token::TypeParameter;
    bool m_hasStaticMod = false;
};

// ----------------------- Document Management ----------------------

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didOpen
struct DidOpen : Server::Method {
    static constexpr FixedString method = "textDocument/didOpen";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#didOpenTextDocumentParams
    struct Params {
        JSON_OBJECT
        lsp::TextDocumentItem JSON_MEMBER(textDocument);
    };

    void handleNotification(Server& server, Params params) {
        server.clientOpenedFile(params.textDocument.path(), std::move(params.textDocument.text));
    }
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didChange
struct DidChange : Server::Method {
    static constexpr FixedString method = "textDocument/didChange";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentContentChangeEvent
    struct ChangeEvent {
        JSON_OBJECT
        std::optional<lsp::Range> JSON_MEMBER(range);
        std::string JSON_MEMBER(text);
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#didChangeTextDocumentParams
    struct Params {
        JSON_OBJECT
        lsp::VersionedTextDocumentIdentifier JSON_MEMBER(textDocument);
        std::vector<ChangeEvent> JSON_MEMBER(contentChanges);
    };

    void handleNotification(Server& server, Params params) {
        if (params.contentChanges.size() != 1 || params.contentChanges.front().range.has_value()) {
            // Incremental updates not supported
            return;
        }
        server.clientChangedFile(params.textDocument.path(), std::move(params.contentChanges.front().text));
    }
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didClose
struct DidClose : Server::Method {
    static constexpr FixedString method = "textDocument/didClose";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#didCloseTextDocumentParams
    struct Params {
        JSON_OBJECT
        lsp::TextDocumentIdentifier JSON_MEMBER(textDocument);
    };

    void handleNotification(Server&, Params) { }
};

// --------------------------- Initialize ---------------------------

template<typename... Ms>
struct MethodCollection { };
template<typename C1, typename C2>
struct MergeMethods;
template<typename... Ms1, typename... Ms2>
struct MergeMethods<MethodCollection<Ms1...>, MethodCollection<Ms2...>> {
    using type = MethodCollection<Ms1..., Ms2...>;
};
//! Methods that don't have client/server caps and an initialize function
using CoreMethods = MethodCollection<DidOpen, DidChange, DidClose>;
//! Language features that can be optionally supported by the server
using ConfigurableMethods = MethodCollection<Hover, GoToDeclaration, SemanticTokens>;
using AllMethods = MergeMethods<CoreMethods, ConfigurableMethods>::type;

template<typename>
struct Tuples;
template<typename... Ms>
struct Tuples<MethodCollection<Ms...>> {
    using ClientCaps = json::Tuple<json::Types<std::optional<typename Ms::ClientCaps>...>, json::Names<Ms::clientCapName...>>;

    using ServerTs = json::Types<std::optional<lsp::TextDocumentSyncOptions>, std::optional<typename Ms::ServerCaps>...>;
    using ServerNs = json::Names<"textDocumentSync", Ms::serverCapName...>;
    using ServerCaps = json::Tuple<ServerTs, ServerNs>;
};
using ClientCapsTuple = Tuples<ConfigurableMethods>::ClientCaps;
using ServerCapsTuple = Tuples<ConfigurableMethods>::ServerCaps;

template<typename M>
concept MethodIsRequest = requires { typename M::Result; };
template<typename M>
concept MethodHasParams = requires { typename M::Params; };
template<typename M>
concept ConfigurableMethod = requires {
    typename M::ClientCaps;
    typename M::ServerCaps;
};
template<typename M>
void forwardHandleMessage(Server::Method& method, Server& server, RequestHandle handle, json::RawDataView params) {
    if constexpr (MethodIsRequest<M>) {
        VERIFY(handle.valid());
        if constexpr (MethodHasParams<M>)
            static_cast<M&>(method).handleRequest(server, handle, json::parse<typename M::Params>(params));
        else
            static_cast<M&>(method).handleRequest(server, handle);
    } else {
        if constexpr (MethodHasParams<M>)
            static_cast<M&>(method).handleNotification(server, json::parse<typename M::Params>(params));
        else
            static_cast<M&>(method).handleNotification(server);
    }
}
template<typename... Ms>
constexpr auto collectMethodInfos(MethodCollection<Ms...>) {
    std::array<Server::MethodInfo, sizeof...(Ms)> result;
    int_t index = 0;
    ((result[index++] = { Ms::method, forwardHandleMessage<Ms> }), ...);
    return result;
}
static constexpr auto METHOD_INFOS = collectMethodInfos(AllMethods());
static constexpr auto HASH_SOLUTION = json::object_detail::findSolution(json::object_detail::toDataVectors(METHOD_INFOS));

template<typename M>
void initializeMethod(Server& server, ServerCapsTuple& serverCapsTuple, const ClientCapsTuple& clientCapsTuple) {
    static constexpr auto tableIndex = json::object_detail::staticEvaluateHash<HASH_SOLUTION>(M::method);
    if constexpr (ConfigurableMethod<M>) {
        const auto& clientCaps = clientCapsTuple.get<M::clientCapName>();
        if (clientCaps.has_value()) {
            auto method = std::make_unique<M>();
            auto serverCaps = method->initialize(server, clientCaps.value());
            if (serverCaps.has_value()) {
                server.m_jumpTable[tableIndex].methodImpl = method.get();
                server.m_methods.emplace_back(std::move(method));
                serverCapsTuple.get<M::serverCapName>() = std::move(serverCaps);
            }
        }
    } else {
        auto method = std::make_unique<M>();
        server.m_jumpTable[tableIndex].methodImpl = method.get();
        server.m_methods.emplace_back(std::move(method));
    }
}

template<typename... Ms>
void initializeMethods(Server& server, ServerCapsTuple& serverCaps, const ClientCapsTuple& clientCaps, MethodCollection<Ms...>) {
    ((initializeMethod<Ms>(server, serverCaps, clientCaps)), ...);
}

void Server::initialize(RequestHandle handle, const lsp::InitializeParams& initParams) {
    static constexpr auto jumpTableBase = json::object_detail::buildJumpTable<HASH_SOLUTION.primeModulo>(HASH_SOLUTION, METHOD_INFOS);

    ClientCapsTuple clientCaps;
    ServerCapsTuple serverCaps;
    if (initParams.capabilities.textDocument.has_value())
        clientCaps = json::parse<ClientCapsTuple>(initParams.capabilities.textDocument.value());

    m_jumpTable.assign(jumpTableBase.begin(), jumpTableBase.end());
    initializeMethods(*this, serverCaps, clientCaps, AllMethods());

    auto& syncOptions = serverCaps.get<"textDocumentSync">();
    syncOptions = lsp::TextDocumentSyncOptions { .openClose = true, .change = lsp::TextDocumentSyncKind::Full };

    std::string serverCapsFmt = json::format(serverCaps);
    lsp::InitializeResult result;
    result.capabilities.data = serverCapsFmt;
    completeRequest(handle, result);

    m_initialized = true;

    acquireBuiltinContext().checkBuiltins();

    // Currently interesting server features are (in order of ease of implementation):
    // - hover:                 https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_hover
    // - declaration:           https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_declaration
    // - semanticTokens:        https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_semanticTokens
    // - highlights:            https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_documentHighlight
    // - implementation:        https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_implementation
    // - completion:            https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion
    // - signatureHelp:         https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_signatureHelp
    // - publishDiagnostics:    https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_publishDiagnostics
    // - diagnostic:            https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_pullDiagnostics
}

// ------------------------ Message Handling ------------------------

void Server::dispatchMessage(const MethodInfo& method, std::string messageData, lsp::IncomingMessage message) {
    RequestHandle handle;
    if (message.id.has_value()) {
        handle.value = m_openRequests.size();
        m_openRequests.push_back({ .requestData = std::move(messageData), .requestId = message.id.value() });
    }
    try {
        method.dispatchFunc(*method.methodImpl, *this, handle, message.params.value_or(json::RawDataView()));
    } catch (...) {
        if (message.id.has_value()) {
            lsp::ResponseMessage response;
            response.id.value = message.id;
            response.error = lsp::ResponseError { .code = lsp::ErrorCode::InternalError, .message = "" };
            std::string responseFmt = json::format(response);
            writeMessage(responseFmt);
        }
    }
}

void Server::completeRequestRaw(RequestHandle handle, json::RawDataView result) {
    lsp::ResponseMessage response;
    response.result = result;
    response.id.value = m_openRequests[handle.value].requestId;
    std::string responseFmt = json::format(response);
    writeMessage(responseFmt);
}

void Server::handleMessage(std::string data) {
    static constexpr MethodInfo INIT_METHOD = {
        "initialize", [](Method&, Server& server, RequestHandle handle, json::RawDataView params) {
            VERIFY(handle.valid());
            server.initialize(handle, json::parse<lsp::InitializeParams>(params));
        }
    };

    auto message = json::parse<lsp::IncomingMessage>(data);

    if (message.id.has_value()) {
        info("Received request with id {} and method {}", json::format(message.id.value()), message.method);
    } else {
        info("Received notification with method {}", message.method);
    }

    if (!m_initialized) {
        if (message.method == INIT_METHOD.name) {
            dispatchMessage(INIT_METHOD, std::move(data), std::move(message));
        } else if (message.method == "exit") {
            VERIFY_NOT_REACHED(); // TODO: Support exit
        } else {
            VERIFY_NOT_REACHED(); // TODO respond with error
        }
        return;
    }

    const MethodInfo& method = m_jumpTable[json::object_detail::staticEvaluateHash<HASH_SOLUTION>(message.method)];
    if (method.name == message.method) {
        dispatchMessage(method, std::move(data), std::move(message));
    }
}

void Server::writeMessage(std::string_view msg) {
    outputBuffer += "Content-Length: ";
    outputBuffer += std::to_string(msg.size());
    outputBuffer += "\r\n\r\n";
    outputBuffer += msg;
}

struct HeaderInfo {
    int_t contentLength = 0;
};
HeaderInfo parseHeader(std::span<const std::string> lines) {
    HeaderInfo result;
    for (std::string_view line : lines) {
        static constexpr std::string_view prefix = "Content-Length:";
        if (line.starts_with(prefix)) {
            std::string_view arg = line.substr(prefix.length());
            while (!arg.empty() && arg.front() == ' ')
                arg = arg.substr(1);
            result.contentLength = json::parse<int32_t>(arg);
        }
    }
    return result;
}

void Server::receiverChacacter(char val) {
    inputBuffer.push_back(val);
    if (remainingContentSize > 0) {
        remainingContentSize -= 1;
        if (remainingContentSize == 0) {
            handleMessage(inputBuffer);
            inputBuffer.clear();
        }
    } else {
        VERIFY(val > 0);
        if (inputBuffer.back() == '\n') {
            VERIFY(inputBuffer.size() > 1);
            VERIFY(inputBuffer[inputBuffer.size() - 2] == '\r');
            inputBuffer.resize(inputBuffer.size() - 2);
            if (inputBuffer.empty()) {
                // Header complete
                HeaderInfo info = parseHeader(parsedHeaderLines);
                VERIFY(info.contentLength > 0);
                remainingContentSize = info.contentLength;
                parsedHeaderLines.clear();
            } else {
                parsedHeaderLines.push_back(inputBuffer);
                inputBuffer.clear();
            }
        }
    }
}

// ------------------------- File utilities -------------------------

Server::FileInfo& Server::fileInfo(const path& filePath) {
    auto it = m_fileCache.find(filePath);
    if (it == m_fileCache.end())
        it = m_fileCache.emplace(filePath).first;
    return const_cast<FileInfo&>(*it);
}

void Server::updateSource(FileInfo& info) {
    if (info.openInClient)
        return;

    auto writeTime = lastWriteTime(info.filePath);
    if (!writeTime.has_value()) {
        // File may not exist anymore, keep the last version of it around
        return;
    }

    if (info.lastWriteTime.has_value() && info.lastWriteTime.value() == writeTime.value()) {
        // File wasn't written to
        return;
    }

    info.setSource(readFile(info.filePath));
    info.lastWriteTime = writeTime;
}

void Server::clientOpenedFile(const path& filePath, std::string fullSource) {
    auto& info = fileInfo(filePath);
    info.openInClient = true;
    info.lastWriteTime = std::nullopt;
    info.setSource(std::move(fullSource));
}

void Server::clientChangedFile(const path& filePath, std::string fullSource) {
    auto& info = fileInfo(filePath);
    if (!info.openInClient)
        return;
    info.setSource(std::move(fullSource));
}

void Server::clientClosedFile(const path& filePath) {
    auto& info = fileInfo(filePath);
    if (!info.openInClient)
        return;

    info.openInClient = false;
    VERIFY(!info.lastWriteTime.has_value());
    updateSource(info);
}

static sema::ProgramHandle scratchProgram(sema::Context& context) {
    return context.programsInModule(context.thisModule()).back();
}

void Server::ensureContext(FileInfo& info, std::span<const sema::ModuleImport> imports) {
    if (info.context != nullptr)
        return;

    info.context = std::make_unique<sema::Context>(imports, info.sourceData);
    auto& context = *info.context;
    context.errorHandler = &semaErrorHandler;
    parse::parseImpl(context.tokenBuffer.source.data(), context, &parseErrorHandler);
    for (auto progHandle : context.programsInModule(context.thisModule()))
        sema::Generator::signatureCheck(context, progHandle);
    auto scratchProg = context.newProgram(sema::ProgramKind::Struct, Word(), parse::TokenHandle(), context.globalNamespace(), SourceLocation());
    VERIFY(scratchProgram(context) == scratchProg);
}

sema::Context& Server::acquireContext(const path& file, std::span<const sema::ModuleImport> imports) {
    FileInfo& info = fileInfo(file);
    updateSource(info);
    ensureContext(info, imports);
    return *info.context;
}

sema::Context& Server::acquireContext(const path& file) {
    std::array imports { acquireBuiltinContext().exportModule() };
    return acquireContext(file, imports);
}

sema::Context& Server::acquireBuiltinContext() {
    return acquireContext(path(COMPILER_TEST_DIR) / "builtins.chrg", {});
}

sema::Util Server::utilFor(sema::Context& context, parse::TokenHandle tokHandle) {
    auto progHandle = context.containingProgram(tokHandle).value_or(scratchProgram(context));
    return sema::Util(context, progHandle);
}

template<typename F>
void Server::forEachToken(sema::Context& context, F&& f) {
    for (int_t i = 0; i < context.tokenBuffer.tokens.size(); i++) {
        // TODO: This is not a very effecient implementation
        parse::TokenHandle tokHandle { (uint32_t)i };
        f(utilFor(context, tokHandle), tokHandle);
    }
}

SourceLocation Server::fromLSP(sema::Context&, lsp::Position pos) {
    // TODO: Implement utf16 to utf8 offset conversion
    return SourceLocation(0, pos.line, pos.character);
}

lsp::Position Server::toLSP(sema::Context&, SourceLocation loc) {
    // TODO: Implement utf16 to utf8 offset conversion
    return lsp::Position { .line = int32_t(loc.lineIndex()), .character = int32_t(loc.offsetInLine()) };
}

}