#include <server/Server.h>
#include <server/json_objects.h>

#include <gtest/gtest.h>

namespace server::test {

struct ServerWrapper {

    void sendMessage(std::string_view msg) {
        auto header = fmt::format("Content-Length: {}\r\n\r\n", msg.size());
        for (char c : header)
            server.receiverChacacter(c);
        for (char c : msg)
            server.receiverChacacter(c);
        // Check that whole message was received
        VERIFY(server.parsedHeaderLines.empty());
        VERIFY(server.inputBuffer.empty());
        VERIFY(server.remainingContentSize == 0);
    }

    std::string sendRequest(std::string_view method, json::RawDataView params) {
        lsp::IncomingMessage msg;
        msg.id = json::IntOrRawStringView(nextRequestId++);
        msg.method = method;
        msg.params = params;
        sendMessage(json::format(msg));
        VERIFY(server.outputBuffer.size() > 0);
        static constexpr std::string_view prefix = "Content-Length: ";
        VERIFY(server.outputBuffer.starts_with(prefix));
        int_t contentLength = json::parse<int32_t>(std::string_view(server.outputBuffer).substr(prefix.length()));
        int_t headerEnd = server.outputBuffer.find("\r\n\r\n") + 4;
        VERIFY((int_t)server.outputBuffer.size() == headerEnd + contentLength);
        std::string result = server.outputBuffer.substr(headerEnd, contentLength);
        server.outputBuffer.clear();
        return result;
    }

    void sendNotification(std::string_view method, json::RawDataView params) {
        lsp::IncomingMessage msg;
        msg.id = json::IntOrRawStringView(nextRequestId++);
        msg.method = method;
        msg.params = params;
        sendMessage(json::format(msg));
        VERIFY(server.outputBuffer.empty());
    }

    Server server;
    int_t nextRequestId = 0;
};

struct Document {
    JSON_OBJECT
    std::string JSON_MEMBER(uri);
    std::string JSON_MEMBER(text);
};

struct Request {
    JSON_OBJECT
    std::string JSON_MEMBER(method);
    json::RawDataView JSON_MEMBER(params);
};

struct Test {
    JSON_OBJECT
    std::vector<Document> JSON_MEMBER(documents);
    Request JSON_MEMBER(request);
    json::RawDataView JSON_MEMBER(result);
};

struct TestFile {
    JSON_OBJECT
    std::vector<Test> JSON_MEMBER(tests);
};

std::string cleanResult(std::string_view input) {
    std::string cleanResult;
    bool insideString = false;
    bool escaped = false;
    for (char c : input) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if (insideString)
                cleanResult.push_back(c);
            continue;
        }
        if (c == '"') {
            if (!escaped)
                insideString = !insideString;
        }
        cleanResult.push_back(c);
        if (c == '\\') {
            escaped = !escaped;
        } else {
            escaped = false;
        }
    }
    return cleanResult;
}

TEST(Server, LSP) {
    namespace fs = std::filesystem;
    auto lspDir = fs::path(COMPILER_TEST_DIR) / "lsp";

    std::string initMessage = readFile(lspDir / "initialize.json");

    for (const auto& entry : fs::directory_iterator(lspDir)) {
        if (!entry.is_regular_file())
            continue;
        if (!entry.path().string().ends_with(".test.json"))
            continue;

        std::string testFileData = readFile(entry.path());
        TestFile testFile = json::parse<TestFile>(testFileData);
        for (const auto& test : testFile.tests) {
            ServerWrapper server;
            server.sendRequest("initialize", { initMessage });
            for (const auto& doc : test.documents) {
                lsp::TextDocumentItem item;
                item.uri = doc.uri;
                item.version = 1;
                item.languageId = "charge";
                item.text = doc.text;
                server.sendNotification("textDocument/didOpen", { "{\"textDocument\":" + json::format(item) + "}" });
            }
            auto responseData = server.sendRequest(test.request.method, test.request.params);
            auto response = json::parse<lsp::ResponseMessage>(responseData);

            EXPECT_EQ(response.result.value_or(json::RawDataView()).data, cleanResult(test.result.data));
        }
    }
}

}