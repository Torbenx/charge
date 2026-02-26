#pragma once

#include <server/Server.h>

namespace server {

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_documentHighlight
struct SemanticHighlight : Server::Method {
    static constexpr FixedString method = "textDocument/documentHighlight";
    static constexpr FixedString clientCapName = "documentHighlight";
    static constexpr FixedString serverCapName = "documentHighlightProvider";

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#documentHighlightClientCapabilities
    struct ClientCaps {
        JSON_OBJECT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#documentHighlightOptions
    struct ServerCaps {
        JSON_OBJECT
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#documentHighlightParams
    using Params = lsp::TextDocumentPositionParams;

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#documentHighlightKind
    enum class HighlightKind {
        Text = 1,
        Read = 2,
        Write = 3,
    };

    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#documentHighlight
    struct Highlight {
        JSON_OBJECT
        lsp::Range JSON_MEMBER(range);
        std::optional<HighlightKind> JSON_MEMBER(kind);
    };

    using Result = json::Nullable<std::vector<Highlight>>;

    std::optional<ServerCaps> initialize(Server&, const ClientCaps&);
    void handleRequest(Server&, RequestHandle, const Params&);

private:
    Result doRequest(Server&, const Params&);
};

}