#pragma once

#include <parse/parse_gen.h>
#include <parse/parse_impl.h>
#include <sema/Context.h>
#include <server/LanguageServerProtocol.h>

#include <filesystem>

namespace server {

struct RequestHandle {
    uint32_t value = std::numeric_limits<uint32_t>::max();

    constexpr bool valid() const { return value != std::numeric_limits<uint32_t>::max(); }
};

using path = std::filesystem::path;
std::string readFile(const path& file);

struct BasicParseErrorHandler : parse::ErrorHandler {
    void invalidToken(parse::LexerToken token, parse::State state, parse::ScopeKind* scopes, sema::Context& context) override {
        println("");
        println(
            "Invalid token '{}' for state '{}' and scope '{}' on line {}",
            parse::nameString(token), parse::nameString(state), parse::nameString(scopes[0]), context.tokenBuffer.lines.size());
        println("scopes:");
        for (;;) {
            println("  {}", parse::nameString(*scopes));
            if (*scopes == parse::ScopeKind::Invalid)
                break;
            scopes -= 1;
        }
        println("");
        VERIFY_NOT_REACHED();
    }
};

struct Server {
    struct Method {
        virtual ~Method() = default;
    };
    using DispatchFunction = void (*)(Method&, Server&, RequestHandle, json::RawDataView);
    struct MethodInfo {
        std::string_view name = {};
        DispatchFunction dispatchFunc = nullptr;
        Server::Method* methodImpl = nullptr;
    };

    using FmtIt = std::back_insert_iterator<std::string>;

    void initialize(RequestHandle handle, const lsp::InitializeParams& initParams);

    void dispatchMessage(const MethodInfo& method, std::string messageData, lsp::IncomingMessage message);
    template<typename T>
    void completeRequest(RequestHandle handle, const T& result) {
        completeRequestRaw(handle, { json::format(result) });
    }
    void completeRequestRaw(RequestHandle handle, json::RawDataView result);

    void run();
    void handleMessage(std::string msg);

    sema::Context& acquireContext(const path& file, std::span<const sema::ModuleImport> imports);
    sema::Context& acquireBuiltinContext();
    sema::Context& acquireContext(const path& file);
    SourceLocation positionToLocation(sema::Context&, lsp::Position);

    template<typename... Args>
    void error(fmt::format_string<Args...> fmtstr, Args&&... args) {
        std::cerr << "Error: " << fmt::format(fmtstr, std::forward<Args>(args)...) << '\n';
    }
    template<typename... Args>
    void fatal(fmt::format_string<Args...> fmtstr, Args&&... args) {
        std::cerr << "Fatal: " << fmt::format(fmtstr, std::forward<Args>(args)...) << '\n';
    }
    template<typename... Args>
    void info(fmt::format_string<Args...> fmtstr, Args&&... args) {
        std::cerr << "Info: " << fmt::format(fmtstr, std::forward<Args>(args)...) << '\n';
    }

    struct RequestInfo {
        std::string requestData; // Must be kept alive for string views
        json::IntOrRawStringView requestId;
    };

    struct ModuleInfo {
        std::string sourceData;
        std::unique_ptr<sema::Context> context;
    };

    bool m_initialized = false;
    std::vector<MethodInfo> m_jumpTable;
    std::vector<RequestInfo> m_openRequests;
    std::vector<std::unique_ptr<Method>> m_methods;
    std::unordered_map<path, ModuleInfo> m_moduleCache;
    BasicParseErrorHandler parseErrorHandler;
};

}