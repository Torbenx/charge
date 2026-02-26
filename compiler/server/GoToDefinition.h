#pragma once

#include <server/Server.h>

namespace server {

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_definition
struct GoToDefinition : Server::Method {
    static constexpr FixedString method = "textDocument/definition";
    static constexpr FixedString clientCapName = "definition";
    static constexpr FixedString serverCapName = "definitionProvider";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#definitionClientCapabilities
    struct ClientCaps {
        JSON_OBJECT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#definitionOptions
    struct ServerCaps {
        JSON_OBJECT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#definitionParams
    using Params = lsp::TextDocumentPositionParams;

    using Result = json::Nullable<lsp::Location>;

    std::optional<ServerCaps> initialize(Server&, const ClientCaps&);
    void handleRequest(Server&, RequestHandle, const Params&);

private:
    Result doRequest(Server&, const Params&);
};

}