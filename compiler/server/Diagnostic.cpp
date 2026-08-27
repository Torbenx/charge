#include <server/Diagnostic.h>

#include <parse/Parser.h>
#include <server/json_objects.h>

namespace server {

std::optional<Diagnostic::ServerCaps> Diagnostic::initialize(Server&, const ClientCaps&) { return ServerCaps(); }

static std::string suggestion(const parse::RecoveredError& error) {
    const auto& r = error.recovery;
    if (r.insertTokens.size() > 1 || r.skipTokens > 1)
        return {};

    std::optional<parse::LexerToken> insertToken;
    if (r.insertTokens.size() > 0)
        insertToken = r.insertTokens.front();
    std::optional<parse::LexerToken> skipToken;
    if (r.skipTokens > 0) {
        const char* pos = error.preRecoveryState.sourcePosition;
        skipToken = parse::lexToken(pos);
    }
    if (insertToken == parse::LexerToken::Identifier || skipToken == parse::LexerToken::Identifier)
        return {};

    if (insertToken.has_value() && skipToken.has_value()) {
        return std::format("Replace '{}' with '{}'?", parse::fixedSpelling(skipToken.value()), parse::fixedSpelling(insertToken.value()));
    } else if (insertToken.has_value()) {
        return std::format("Missing '{}'?", parse::fixedSpelling(insertToken.value()));
    } else if (skipToken.has_value()) {
        return std::format("Excess '{}'?", parse::fixedSpelling(skipToken.value()));
    } else {
        VERIFY_NOT_REACHED();
    }
}

Diagnostic::Result Diagnostic::doRequest(Server& server, const Params& params) {
    auto& context = server.acquireContext(params.textDocument.path());
    std::vector<lsp::Diagnostic> result;
    for (const auto& error : context.parseErrors()) {
        auto range = error.errorRange();
        SourceLocation startLoc = context.tokenBuffer.findSourceLocation(range.begin());
        SourceLocation endLoc = context.tokenBuffer.findSourceLocation(range.end());

        auto sug = suggestion(error);
        result.push_back(lsp::Diagnostic {
            .range = { server.toLSP(context, startLoc), server.toLSP(context, endLoc) },
            .severity = lsp::DiagnosticSeverity::Error,
            .code = std::nullopt,
            .source = std::nullopt,
            .message = sug.empty() ? "Syntax error" : std::format("Syntax error: {}", sug),
        });
    }
    return {
        .kind = "full",
        .items = std::move(result)
    };
}

void Diagnostic::handleRequest(Server& server, RequestHandle handle, const Params& params) {
    server.completeRequest(handle, doRequest(server, params));
}

}