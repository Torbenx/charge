#pragma once

#include <server/json.h>

namespace server::lsp {

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#requestMessage
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#notificationMessage
struct IncomingMessage {
    JSON_OBJECT
    json::RawStringView JSON_MEMBER(jsonrpc) = "2.0";
    std::optional<json::IntOrRawStringView> JSON_MEMBER(id);
    std::string JSON_MEMBER(method);
    std::optional<json::RawDataView> JSON_MEMBER(params);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#errorCodes
enum class ErrorCode : int32_t {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,

    jsonrpcReservedErrorRangeStart = -32099,
    ServerNotInitialized = -32002,
    UnknownErrorCode = -32001,
    jsonrpcReservedErrorRangeEnd = -32000,

    lspReservedErrorRangeStart = -32899,
    RequestFailed = -32803,
    ServerCancelled = -32802,
    ContentModified = -32801,
    RequestCancelled = -32800,
    lspReservedErrorRangeEnd = -32800,
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#responseError
struct ResponseError {
    JSON_OBJECT
    ErrorCode JSON_MEMBER(code);
    std::string JSON_MEMBER(message);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#responseMessage
struct ResponseMessage {
    JSON_OBJECT
    json::RawStringView JSON_MEMBER(jsonrpc) = "2.0";
    json::Nullable<json::IntOrRawStringView> JSON_MEMBER(id);
    std::optional<json::RawDataView> JSON_MEMBER(result);
    std::optional<ResponseError> JSON_MEMBER(error);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#position
struct Position {
    JSON_OBJECT
    int32_t JSON_MEMBER(line);
    int32_t JSON_MEMBER(character);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#range
struct Range {
    JSON_OBJECT
    Position JSON_MEMBER(start);
    Position JSON_MEMBER(end);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentItem
struct TextDocumentItem {
    JSON_OBJECT
    std::string JSON_MEMBER(uri);
    std::string JSON_MEMBER(languageId);
    int32_t JSON_MEMBER(version);
    std::string JSON_MEMBER(text);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentIdentifier
struct TextDocumentIdentifier {
    JSON_OBJECT
    std::string JSON_MEMBER(uri);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#versionedTextDocumentIdentifier
struct VersionedTextDocumentIdentifier {
    JSON_OBJECT
    std::string JSON_MEMBER(uri);
    int32_t JSON_MEMBER(version);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#optionalVersionedTextDocumentIdentifier
struct OptionalVersionedTextDocumentIdentifier {
    JSON_OBJECT
    std::string JSON_MEMBER(uri);
    json::Nullable<int32_t> JSON_MEMBER(version);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textEdit
struct TextEdit {
    JSON_OBJECT
    Range JSON_MEMBER(range);
    std::string JSON_MEMBER(newText);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentEdit
struct TextDocumentEdit {
    JSON_OBJECT
    OptionalVersionedTextDocumentIdentifier JSON_MEMBER(textDocument);
    std::vector<TextEdit> JSON_MEMBER(edits);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#location
struct Location {
    JSON_OBJECT
    std::string JSON_MEMBER(uri);
    Range JSON_MEMBER(range);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#diagnosticSeverity
enum class DiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#diagnosticRelatedInformation
struct DiagnosticRelatedInformation {
    JSON_OBJECT
    Location JSON_MEMBER(location);
    std::string JSON_MEMBER(message);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#diagnostic
struct Diagnostic {
    JSON_OBJECT
    Range JSON_MEMBER(range);
    std::optional<DiagnosticSeverity> JSON_MEMBER(severity);
    std::optional<json::IntOrRawStringView> JSON_MEMBER(code);
    std::optional<std::string> JSON_MEMBER(source);
    std::string JSON_MEMBER(message);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#workspaceEdit
struct WorkspaceEdit {
    JSON_OBJECT
    std::vector<TextDocumentEdit> JSON_MEMBER(documentChanges);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#workspaceFolder
struct WorkspaceFolder {
    JSON_OBJECT
    std::string JSON_MEMBER(uri);
    std::string JSON_MEMBER(name);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#markdownClientCapabilities
struct MarkdownClientCapabilities {
    JSON_OBJECT
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#clientCapabilities
struct ClientCapabilities {
    struct General {
        JSON_OBJECT
        std::optional<std::vector<std::string>> JSON_MEMBER(positionEncodings);
        std::optional<MarkdownClientCapabilities> JSON_MEMBER(markdown);
    };

    struct Workspace {
        JSON_OBJECT
        std::optional<bool> JSON_MEMBER(workspaceFolders);
    };

    JSON_OBJECT
    std::optional<General> JSON_MEMBER(general);
    std::optional<Workspace> JSON_MEMBER(workspace);
    std::optional<json::RawDataView> JSON_MEMBER(textDocument);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#initializeParams
struct InitializeParams {
    JSON_OBJECT
    json::Nullable<int32_t> JSON_MEMBER(processId);

    std::optional<json::Nullable<std::string>> JSON_MEMBER(rootPath);
    std::optional<json::Nullable<std::string>> JSON_MEMBER(rootUri);
    std::optional<std::vector<WorkspaceFolder>> JSON_MEMBER(workspaceFolders);
    std::vector<WorkspaceFolder> computeFolders() const {
        if (workspaceFolders.has_value()) {
            return workspaceFolders.value();
        } else if (rootUri.has_value()) {
            if (rootUri.value().value.has_value())
                return { WorkspaceFolder { .uri = rootUri.value().value.value(), .name = {} } };
        } else if (rootPath.has_value()) {
            if (rootPath.value().value.has_value())
                return { WorkspaceFolder { .uri = "file://" + rootPath.value().value.value(), .name = {} } };
        }
        return {};
    }

    std::optional<json::RawDataView> JSON_MEMBER(initializationOptions);
    ClientCapabilities JSON_MEMBER(capabilities);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentSyncKind
enum class TextDocumentSyncKind : int32_t {
    None = 0,
    Full = 1,
    Incremental = 2,
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentSyncOptions
struct TextDocumentSyncOptions {
    JSON_OBJECT
    std::optional<bool> JSON_MEMBER(openClose);
    std::optional<TextDocumentSyncKind> JSON_MEMBER(change);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#initializeResult
struct InitializeResult {
    JSON_OBJECT
    json::RawDataView JSON_MEMBER(capabilities);
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#markupContent
struct MarkupKind {
    static constexpr std::string_view PlainText = "plaintext";
    static constexpr std::string_view Markdown = "markdown";
    MarkupKind() = delete;
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#markupContentInnerDefinition
struct MarkupContent {
    JSON_OBJECT
    std::string JSON_MEMBER(kind);
    std::string JSON_MEMBER(value);
};

}