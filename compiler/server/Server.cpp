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

    void handleRequest(Server& server) {
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

template<typename... Ms>
constexpr auto collectMethodInfos(MethodCollection<Ms...>) {
    std::array<Server::MethodInfo, sizeof...(Ms)> result;
    int_t index = 0;
    ((result[index++] = { Ms::method, [](Server::Method& method, Server& server) { static_cast<Ms&>(method).handleRequest(server); } }), ...);
    return result;
}

template<json::object_detail::Solution sol, typename M>
void initializeMethod(Server& server, ServerCapsTuple& serverCapsTuple, const ClientCapsTuple& clientCapsTuple) {
    static constexpr auto tableIndex = json::object_detail::staticEvaluateHash<sol>(M::method);
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

template<json::object_detail::Solution sol, typename... Ms>
void initializeMethods(Server& server, ServerCapsTuple& serverCaps, const ClientCapsTuple& clientCaps, MethodCollection<Ms...>) {
    ((initializeMethod<sol, Ms>(server, serverCaps, clientCaps)), ...);
}

void Server::initialize(const lsp::InitializeParams& initParams) {
    using namespace json::object_detail;
    static constexpr auto methodInfos = collectMethodInfos(Methods());
    static constexpr Solution solution = findSolution(toDataVectors(methodInfos));
    static constexpr auto jumpTable = buildJumpTable<solution.primeModulo>(solution, methodInfos);

    ClientCapsTuple clientCaps;
    ServerCapsTuple serverCaps;
    if (initParams.capabilities.textDocument.has_value())
        clientCaps = json::parse<ClientCapsTuple>(initParams.capabilities.textDocument.value());

    m_jumpTable.assign(jumpTable.begin(), jumpTable.end());
    initializeMethods<solution>(*this, serverCaps, clientCaps, Methods());

    std::string serverCapsFmt = json::format(serverCaps);
    lsp::InitializeResult result;
    result.capabilities.data = serverCapsFmt;
    std::string resultFmt = json::format(result);
    lsp::ResponseMessage response;
    response.result = json::RawDataView(resultFmt);
    response.id.value = json::IntOrRawStringView(0);
    std::string responseFmt = json::format(response);

    std::cout << "Content-Length: " << responseFmt.size() << "\r\n\r\n";
    std::cout.write(responseFmt.data(), responseFmt.size());

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

void Server::handleMessage(std::string data) {
    auto message = json::parse<lsp::IncomingMessage>(data);
    info("Received message id {} with method {}", message.id.getInt(), message.method.data);
    if (message.method.data == "initialize" && message.params.has_value()) {
        initialize(json::parse<lsp::InitializeParams>(message.params.value()));
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