#pragma once

#include <server/LanguageServerProtocol.h>
#include <server/SemaContext.h>

#include <filesystem>
#include <unordered_set>

namespace server {

using path = std::filesystem::path;
using file_time = std::filesystem::file_time_type;

}

template<>
struct optional_traits<server::file_time> {
    static constexpr auto empty_value = server::file_time::min();
};

namespace server {

inline std::optional<file_time> lastWriteTime(const path& filePath) {
    std::error_code code;
    return std::filesystem::last_write_time(filePath, code);
}

struct RequestHandle {
    uint32_t value = std::numeric_limits<uint32_t>::max();

    constexpr bool valid() const { return value != std::numeric_limits<uint32_t>::max(); }
};

std::string readFile(const path& file);

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

    void initialize(RequestHandle handle, const lsp::InitializeParams& initParams);

    void dispatchMessage(const MethodInfo& method, std::string messageData, lsp::IncomingMessage message);
    template<typename T>
    void completeRequest(RequestHandle handle, const T& result) {
        completeRequestRaw(handle, { json::format(result) });
    }
    void completeRequestRaw(RequestHandle handle, json::RawDataView result);

    bool shouldExit() const { return false; }
    void receiverChacacter(char c);
    void handleMessage(std::string msg);
    void writeMessage(std::string_view msg);

    SemaContext& acquireContext(const path& file, std::span<const sema::ModuleImport> imports);
    SemaContext& acquireBuiltinContext();
    SemaContext& acquireContext(const path& file);
    SourceLocation fromLSP(sema::Context&, lsp::Position);
    lsp::Position toLSP(sema::Context&, SourceLocation);

    template<typename... Args>
    void error(std::format_string<Args...> fmtstr, Args&&... args) {
        std::cerr << "Error: " << std::format(fmtstr, std::forward<Args>(args)...) << '\n';
    }
    template<typename... Args>
    void fatal(std::format_string<Args...> fmtstr, Args&&... args) {
        std::cerr << "Fatal: " << std::format(fmtstr, std::forward<Args>(args)...) << '\n';
    }
    template<typename... Args>
    void info(std::format_string<Args...> fmtstr, Args&&... args) {
        std::cerr << "Info: " << std::format(fmtstr, std::forward<Args>(args)...) << '\n';
    }

    struct RequestInfo {
        //! The data of the original message
        /*!
        Must be kept alive for string views.
        TODO: SSO could bite us here because we rely on pointer stability.
        */
        std::string requestData;
        json::IntOrRawStringView requestId;
    };

    struct FileInfo {
        const path filePath;
        // TODO: SSO could bite us here because we rely on pointer stability of the source data.
        std::string sourceData;
        std::unique_ptr<SemaContext> context;
        bool openInClient = false;
        std::optional<file_time> lastWriteTime;

        explicit FileInfo(const path& filePath)
            : filePath(filePath) { }

        void setSource(std::string newSourceData) {
            if (sourceData != newSourceData) {
                sourceData = std::move(newSourceData);
                context.reset();
            }
        }
    };
    struct FileInfoHash {
        using is_transparent = void;

        size_t operator()(const FileInfo& info) const {
            return std::hash<path>()(info.filePath);
        }
        size_t operator()(const path& filePath) const {
            return std::hash<path>()(filePath);
        }
    };
    struct FileInfoEqual {
        using is_transparent = void;

        bool operator()(const FileInfo& l, const FileInfo& r) const {
            return l.filePath == r.filePath;
        }
        bool operator()(const path& filePath, const FileInfo& info) const {
            return filePath == info.filePath;
        }
    };

    struct SemaErrorHandler : sema::ErrorHandler {
        void handleError(sema::Generator&, sema::ErrorBase&) override {
            // TODO: Errors ignored
        }
    };

    FileInfo& fileInfo(const path& filePath);
    void updateSource(FileInfo& info);
    void clientOpenedFile(const path& filePath, std::string fullSource);
    void clientChangedFile(const path& filePath, std::string fullSource);
    void clientClosedFile(const path& filePath);
    void ensureContext(FileInfo& info, std::span<const sema::ModuleImport> imports);

    bool m_initialized = false;
    std::vector<MethodInfo> m_jumpTable;
    std::vector<RequestInfo> m_openRequests;
    std::vector<std::unique_ptr<Method>> m_methods;
    std::unordered_set<FileInfo, FileInfoHash, FileInfoEqual> m_fileCache;
    SemaErrorHandler semaErrorHandler;

    int_t remainingContentSize = 0;
    std::string inputBuffer;
    std::vector<std::string> parsedHeaderLines;
    std::string outputBuffer;
};

}