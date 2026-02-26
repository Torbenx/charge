#pragma once

#include <server/Server.h>

namespace server {

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

    std::optional<ServerCaps> initialize(Server&, const ClientCaps&);
    void handleRequest(Server&, RequestHandle, const Params&);

private:
    Result doRequest(Server&, const Params&);
};

}