#include <server/Server.h>

#include <parse/parse_impl.h>
#include <sema/Formatter.h>
#include <sema/Generator.h>
#include <server/json_objects.h>
#include <server/json_tuple.h>

#include <filesystem>
#include <fstream>

#ifdef WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace server {

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
    sourceBuffer.resize(length + 2);
    stream.seekg(0, std::ios::beg);
    stream.read(sourceBuffer.data(), length);
    stream.close();
    VERIFY(stream.good()); // TODO: Should not verify on user data

    return sourceBuffer;
}

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
    struct Params {
        JSON_OBJECT
        lsp::TextDocumentIdentifier JSON_MEMBER(textDocument);
        lsp::Position JSON_MEMBER(position);
    };

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
        println("Extraction {} {}", std::to_underlying(e.kind()), e.id());
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
        auto location = server.positionToLocation(context, params.position);
        auto tokHandle = context.tokenBuffer.findContainingToken(location);
        if (!tokHandle.has_value())
            return {};

        auto maybeProg = context.lastDeclarationAtOrBefore(location);
        if (!maybeProg.has_value())
            return {};
        sema::Util util(context, maybeProg.value());
        VERIFY(util.program->declarationLocation() < location); // TODO: A range check would be better

        sema::Formatter formatter { util };
        auto token = context.tokenBuffer.token(tokHandle.value());
        if (token.hasData2<sema::Expression>()) {
            auto staticInfo = extractStaticInfo(util, token.data2<sema::Expression>());
            if (std::holds_alternative<sema::Constant>(staticInfo)) {
                sema::Constant c = std::get<sema::Constant>(staticInfo);
                if (!formatter.formatAsDeclaration(c))
                    formatter.formatConstant(c);
            } else if (std::holds_alternative<VariableInfo>(staticInfo)) {
                auto [name, type, category] = std::get<VariableInfo>(staticInfo);
                formatter.formatVariableDeclaration(name, type, category);
            }
        } else if (parse::isStaticDecl(token.kind())) {
            formatter.formatAsDeclaration(sema::Constant(util.program->selfConstant()));
        } else if (parse::isEnumValueDecl(token.kind())) {
            auto valueIndex = token.data2<uint32_t>();
            formatter.formatEnumValueDeclaration(sema::Constant(util.program->selfConstant()), valueIndex);
        } else if (parse::isMemberDecl(token.kind())) {
            auto memberIndex = token.kind() == parse::TokenKind::HasMemberDecl ? token.data1<uint32_t>() : token.data2<uint32_t>();
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

template<typename... Ms>
struct MethodCollection { };
using Methods = MethodCollection<Hover>;

template<typename>
struct Tuples;
template<typename... Ms>
struct Tuples<MethodCollection<Ms...>> {
    using ClientCaps = json::Tuple<json::Types<std::optional<typename Ms::ClientCaps>...>, json::Names<Ms::clientCapName...>>;

    using ServerTs = json::Types<std::optional<lsp::TextDocumentSyncOptions>, std::optional<typename Ms::ServerCaps>...>;
    using ServerNs = json::Names<"textDocumentSync", Ms::serverCapName...>;
    using ServerCaps = json::Tuple<ServerTs, ServerNs>;
};
using ClientCapsTuple = Tuples<Methods>::ClientCaps;
using ServerCapsTuple = Tuples<Methods>::ServerCaps;

template<typename M>
concept MethodIsRequest = requires { typename M::Result; };
template<typename M>
concept MethodHasParams = requires { typename M::Params; };
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
static constexpr auto METHOD_INFOS = collectMethodInfos(Methods());
static constexpr auto HASH_SOLUTION = json::object_detail::findSolution(json::object_detail::toDataVectors(METHOD_INFOS));

template<typename M>
void initializeMethod(Server& server, ServerCapsTuple& serverCapsTuple, const ClientCapsTuple& clientCapsTuple) {
    static constexpr auto tableIndex = json::object_detail::staticEvaluateHash<HASH_SOLUTION>(M::method);
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
}

template<typename... Ms>
void initializeMethods(Server& server, ServerCapsTuple& serverCaps, const ClientCapsTuple& clientCaps, MethodCollection<Ms...>) {
    ((initializeMethod<Ms>(server, serverCaps, clientCaps)), ...);
}

static void parseModule(Server& server, sema::Context& context) {
    parse::parseImpl(context.tokenBuffer.source.data(), context, &server.parseErrorHandler);
}

void Server::initialize(RequestHandle handle, const lsp::InitializeParams& initParams) {
    static constexpr auto jumpTableBase = json::object_detail::buildJumpTable<HASH_SOLUTION.primeModulo>(HASH_SOLUTION, METHOD_INFOS);

    ClientCapsTuple clientCaps;
    ServerCapsTuple serverCaps;
    if (initParams.capabilities.textDocument.has_value())
        clientCaps = json::parse<ClientCapsTuple>(initParams.capabilities.textDocument.value());

    m_jumpTable.assign(jumpTableBase.begin(), jumpTableBase.end());
    initializeMethods(*this, serverCaps, clientCaps, Methods());

    std::string serverCapsFmt = json::format(serverCaps);
    lsp::InitializeResult result;
    result.capabilities.data = serverCapsFmt;
    completeRequest(handle, result);

    m_initialized = true;

    acquireBuiltinContext().checkBuiltins();

    // Currently interesting server features are:
    // - completion:            https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion
    // - hover:                 https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_hover
    // - signatureHelp:         https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_signatureHelp
    // - declaration:           https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_declaration
    // - definition:            https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_definition
    // - typeDefinition (same as definition?): https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_typeDefinition
    // - implementation:        https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_implementation
    // - publishDiagnostics:    https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_publishDiagnostics
    // - diagnostic:            https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_pullDiagnostics
    // - semanticTokens         https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokensClientCapabilities
}

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
            std::cout << "Content-Length: " << responseFmt.size() << "\r\n\r\n";
            std::cout.write(responseFmt.data(), responseFmt.size());
        }
    }
}

void Server::completeRequestRaw(RequestHandle handle, json::RawDataView result) {
    lsp::ResponseMessage response;
    response.result = result;
    response.id.value = m_openRequests[handle.value].requestId;
    std::string responseFmt = json::format(response);
    std::cout << "Content-Length: " << responseFmt.size() << "\r\n\r\n";
    std::cout.write(responseFmt.data(), responseFmt.size());
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

void Server::run() {
#ifdef WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    std::string buffer;
    std::vector<std::string> headerLines;
    for (;;) {
        auto val = std::cin.get();
        if (std::cin.fail()) {
            std::cerr << "Reading stdin failed\n";
            break;
        }
        VERIFY(val > 0 && val < 128);
        buffer.push_back(val);
        if (buffer.back() == '\n') {
            VERIFY(buffer.size() > 1);
            VERIFY(buffer[buffer.size() - 2] == '\r');
            buffer.resize(buffer.size() - 2);
            if (buffer.empty()) {
                // Header complete
                auto info = parseHeader(headerLines);
                std::string data;
                data.resize(info.contentLength);
                std::cin.read(data.data(), data.size());
                if (std::cin.fail()) {
                    std::cerr << "Reading stdin failed\n";
                    break;
                }
                handleMessage(std::move(data));
            } else {
                headerLines.push_back(buffer);
                buffer.clear();
            }
        }
    }
}

sema::Context& Server::acquireContext(const path& file, std::span<const sema::ModuleImport> imports) {
    auto it = m_moduleCache.find(file);
    if (it == m_moduleCache.end()) {
        auto [newIt, isNew] = m_moduleCache.emplace(file, ModuleInfo { readFile(file), nullptr });
        VERIFY(isNew);
        it = newIt;

        it->second.context = std::make_unique<sema::Context>(imports, it->second.sourceData);
        auto& context = *it->second.context;
        parseModule(*this, context);
        for (auto progHandle : context.programsInModule(context.thisModule()))
            sema::Generator::signatureCheck(context, progHandle);
    }
    return *it->second.context;
}

sema::Context& Server::acquireContext(const path& file) {
    std::array imports { acquireBuiltinContext().exportModule() };
    return acquireContext(file, imports);
}

sema::Context& Server::acquireBuiltinContext() {
    return acquireContext(path(COMPILER_TEST_DIR) / "builtins.chrg", {});
}

SourceLocation Server::positionToLocation(sema::Context&, lsp::Position pos) {
    // TODO: Implement utf16 to utf8 offset conversion
    return SourceLocation(0, pos.line, pos.character);
}

}