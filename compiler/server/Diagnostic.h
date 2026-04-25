#pragma once

#include <server/Server.h>

namespace server {

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_pullDiagnostics
struct Diagnostic : Server::Method {
    static constexpr FixedString method = "textDocument/diagnostic";
    static constexpr FixedString clientCapName = "diagnostic";
    static constexpr FixedString serverCapName = "diagnosticProvider";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#diagnosticClientCapabilities
    struct ClientCaps {
        JSON_OBJECT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#diagnosticOptions
    struct ServerCaps {
        JSON_OBJECT
        bool JSON_MEMBER(interFileDependencies) = false;
        bool JSON_MEMBER(workspaceDiagnostics) = false;
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#documentDiagnosticParams
    struct Params {
        JSON_OBJECT
        lsp::TextDocumentIdentifier JSON_MEMBER(textDocument);
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#documentDiagnosticReport
    struct Result {
        JSON_OBJECT
        std::string JSON_MEMBER(kind); // 'full' or 'unchanged'
        std::optional<std::vector<lsp::Diagnostic>> JSON_MEMBER(items);
    };

    std::optional<ServerCaps> initialize(Server&, const ClientCaps&);
    void handleRequest(Server&, RequestHandle, const Params&);

private:
    Result doRequest(Server&, const Params&);
};

}