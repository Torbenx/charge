#include <server/Server.h>

#include <parse/parse_impl.h>
#include <sema/Formatter.h>
#include <sema/Generator.h>
#include <server/json_objects.h>
#include <server/json_tuple.h>

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

        auto maybeProg = context.containingProgram(tokHandle.value());
        if (!maybeProg.has_value())
            return {}; // TODO: A program is not required for formatting namespace declarations
        sema::Util util(context, maybeProg.value());

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
using ConfigurableMethods = MethodCollection<Hover>;
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
    // - definition:            https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_definition
    // - typeDefinition (same as definition?): https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_typeDefinition
    // - semanticTokens:        https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#semanticTokensClientCapabilities
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
        VERIFY(val > 0 && val < 128);
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

void Server::ensureContext(FileInfo& info, std::span<const sema::ModuleImport> imports) {
    if (info.context != nullptr)
        return;

    info.context = std::make_unique<sema::Context>(imports, info.sourceData);
    auto& context = *info.context;
    context.errorHandler = &semaErrorHandler;
    parse::parseImpl(context.tokenBuffer.source.data(), context, &parseErrorHandler);
    for (auto progHandle : context.programsInModule(context.thisModule()))
        sema::Generator::signatureCheck(context, progHandle);
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

SourceLocation Server::fromLSP(sema::Context&, lsp::Position pos) {
    // TODO: Implement utf16 to utf8 offset conversion
    return SourceLocation(0, pos.line, pos.character);
}

}