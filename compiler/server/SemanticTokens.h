#pragma once

#include <server/Server.h>

#include <bitset>

namespace server {

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_semanticTokens
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

    std::optional<ServerCaps> initialize(Server&, const ClientCaps&);
    void handleRequest(Server&, RequestHandle, const Params&);

private:
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

    Result doRequest(Server&, const Params&);
    static std::string_view lspName(Token);
    bool enabled(Token t) const { return m_enableMask[std::to_underlying(t)]; }

    std::bitset<std::to_underlying(Token::COUNT)> m_enableMask;
    Token m_enumToken = Token::Enum;
    Token m_structToken = Token::Struct;
    Token m_typeParameterToken = Token::TypeParameter;
    bool m_hasStaticMod = false;
};

}