#include <server/GoToDefinition.h>

#include <server/json_objects.h>

namespace server {

std::optional<GoToDefinition::ServerCaps> GoToDefinition::initialize(Server&, const ClientCaps&) { return ServerCaps(); }

GoToDefinition::Result GoToDefinition::doRequest(Server& server, const Params& params) {
    auto& context = server.acquireContext(params.textDocument.path());
    auto location = server.fromLSP(context, params.position);
    auto tokHandle = context.containingIdentifier(location);
    if (!tokHandle.has_value())
        return {};
    auto token = context.tokenBuffer.token(tokHandle.value());
    auto util = context.utilFor(tokHandle.value());

    auto declInfo = util.extractDeclarationInfo(token);
    auto maybeLoc = util.declarationLocation(declInfo);
    if (!maybeLoc.has_value())
        return {};

    {
        SourceLocation location = maybeLoc.value();
        auto tokHandle = context.tokenBuffer.findPrecedingToken(location);
        VERIFY(tokHandle.has_value());
        int_t length = context.tokenBuffer.tokenSpelling(context.tokenBuffer.token(tokHandle.value())).length();
        lsp::Position pos = server.toLSP(context, location);
        lsp::Range range { pos, lsp::Position { .line = pos.line, .character = int32_t(pos.character + length) } };
        return { lsp::Location {
            .uri = params.textDocument.uri,
            .range = range,
        } };
    }
}

void GoToDefinition::handleRequest(Server& server, RequestHandle handle, const Params& params) {
    server.completeRequest(handle, doRequest(server, params));
}

}