#pragma once

#include <server/Server.h>

namespace server {

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

    std::optional<ServerCaps> initialize(Server&, const ClientCaps&);
    void handleRequest(Server&, RequestHandle, const Params&);

private:
    Result doRequest(Server&, const Params&);

    bool m_useMarkdown = false;
};

}