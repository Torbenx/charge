#include <server/Server.h>

#include <server/json_objects.h>
#include <server/json_tuple.h>

#include <fcntl.h>
#include <io.h>

namespace server {

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_hover
struct Hover : Server::Method {
    static constexpr json::FixedString clientCapName = "hover";
    static constexpr json::FixedString serverCapName = "hoverProvider";
    static constexpr json::FixedString method = "textDocument/hover";

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
        server.info("Hover initialized to use {}", m_useMarkdown ? "markdown" : "plaintext");

        // Successful initialize
        return ServerCaps {};
    }

    void handleRequest(Server& server, RequestHandle handle, const Params& params) {
        server.info("Hover params: {}", json::format(params));
        HoverResult result;
        result.contents.kind = lsp::MarkupKind::Markdown;
        result.contents.value = "Some dummy text";
        server.completeRequest(handle, result);
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
    method.dispatchFunc(*method.methodImpl, *this, handle, message.params.value_or(json::RawDataView()));
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
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

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

}