#include <server/SemanticHighlight.h>

#include <server/json_objects.h>

namespace server {

std::optional<SemanticHighlight::ServerCaps> SemanticHighlight::initialize(Server&, const ClientCaps&) {
    return ServerCaps();
}

SemanticHighlight::Result SemanticHighlight::doRequest(Server& server, const Params& params) {
    std::vector<Highlight> result;
    auto& context = server.acquireContext(params.textDocument.path());
    auto tokHandle = context.containingIdentifier(server.fromLSP(context, params.position));
    if (!tokHandle.has_value())
        return { result };

    auto sourceUtil = context.utilFor(tokHandle.value());
    auto token = context.tokenBuffer.token(tokHandle.value());
    auto sourceInfo = sourceUtil.extractDeclarationInfo(token);
    if (std::holds_alternative<std::monostate>(sourceInfo))
        return { result };

    context.forEachToken([&](SemaUtil& targetUtil, parse::TokenHandle targetTokHandle) {
        auto targetToken = context.tokenBuffer.token(targetTokHandle);
        auto targetInfo = targetUtil.extractDeclarationInfo(targetToken);
        if (targetInfo != sourceInfo)
            return;
        lsp::Position pos = server.toLSP(context, targetToken.location());
        int_t length = context.tokenBuffer.tokenSpelling(targetToken).length();
        lsp::Position endPos = { .line = pos.line, .character = int32_t(pos.character + length) };
        result.push_back(Highlight { .range = { pos, endPos }, .kind = std::nullopt });
    });
    return { result };
}

void SemanticHighlight::handleRequest(Server& server, RequestHandle handle, const Params& params) {
    server.completeRequest(handle, doRequest(server, params));
}

}