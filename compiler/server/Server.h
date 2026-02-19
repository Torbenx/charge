#pragma once

#include <ReverseMemberPointer.h>
#include <server/LanguageServerProtocol.h>

namespace server {

struct Server;

struct RequestHandle {
    uint32_t value = std::numeric_limits<uint32_t>::max();

    constexpr bool valid() const { return value != std::numeric_limits<uint32_t>::max(); }
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

    void dispatchMessage(const MethodInfo& method, std::string messageData, lsp::IncomingMessage message);
    template<typename T>
    void completeRequest(RequestHandle handle, const T& result) {
        completeRequestRaw(handle, { json::format(result) });
    }
    void completeRequestRaw(RequestHandle handle, json::RawDataView result);
    void initialize(RequestHandle handle, const lsp::InitializeParams& initParams);
    void run();
    void handleMessage(std::string msg);

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

    bool m_initialized = false;
    std::vector<MethodInfo> m_jumpTable;
    std::vector<RequestInfo> m_openRequests;
    std::vector<std::unique_ptr<Method>> m_methods;
};

}