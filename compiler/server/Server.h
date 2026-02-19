#pragma once

#include <ReverseMemberPointer.h>
#include <server/LanguageServerProtocol.h>

namespace server {

struct Server;

struct RequestHandle {
    uint32_t value;
};

struct Server {
    struct Method {
        virtual ~Method() = default;
    };
    using DispatchFunction = void (*)(Method&, Server&);
    struct MethodInfo {
        std::string_view name = {};
        DispatchFunction dispatchFunc = nullptr;
        Server::Method* methodImpl = nullptr;
    };

    void initialize(const lsp::InitializeParams& initParams);
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

    std::vector<MethodInfo> m_jumpTable;
    std::vector<std::unique_ptr<Method>> m_methods;
};

}