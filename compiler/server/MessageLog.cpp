#include <server/MessageLog.h>

#include <server/Server.h>

#include <charconv>
#include <cstdlib>
#include <format>
#include <thread>
#include <vector>

namespace server {

// ---------------------------- Framing -----------------------------

static constexpr std::string_view LENGTH_HEADER = "Content-Length: ";
static constexpr std::string_view TIME_HEADER = "X-Charge-Time: ";
static constexpr std::string_view EVENT_HEADER = "X-Charge-Event: ";

static std::string_view eventName(MessageLog::Event event) {
    switch (event) {
    case MessageLog::Event::Incoming:
        return "incoming";
    case MessageLog::Event::Outgoing:
        return "outgoing";
    case MessageLog::Event::Handled:
        return "handled";
    }
    VERIFY_NOT_REACHED();
}

// --------------------------- Recording ----------------------------

std::unique_ptr<MessageLog> MessageLog::createFromEnvironment() {
    const char* directory = std::getenv("CHARGE_LSP_RECORD");
    if (directory == nullptr || directory[0] == '\0')
        return nullptr;

    // The timestamp is in UTC so that the names sort chronologically
    auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    auto logFile = std::filesystem::path(directory) / std::format("lsp-{:%Y%m%d-%H%M%S}.log", now);

    auto log = std::make_unique<MessageLog>(logFile);
    if (!log->stream.good()) {
        dbgln("Could not open message log {}, recording disabled", logFile.string());
        return nullptr;
    }
    dbgln("Recording messages to {}", logFile.string());
    return log;
}

MessageLog::MessageLog(const std::filesystem::path& logFile) {
    stream.open(logFile, std::ios::binary | std::ios::trunc);
}

void MessageLog::record(Event event, std::string_view content) {
    if (limitReached)
        return;
    if (writtenSize >= SIZE_LIMIT) {
        limitReached = true;
        dbgln("Message log reached the limit of {} bytes, recording stopped", SIZE_LIMIT);
        return;
    }

    auto time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
    auto header = std::format("{}{}\r\n{}{}\r\n{}{}\r\n\r\n",
        LENGTH_HEADER, content.size(),
        TIME_HEADER, time.count(),
        EVENT_HEADER, eventName(event));

    stream.write(header.data(), header.size());
    stream.write(content.data(), content.size());
    // A crash while handling a message must not lose that message
    stream.flush();

    writtenSize += header.size() + content.size();
}

// ---------------------------- Replaying ---------------------------

namespace {

    struct LogEntry {
        int64_t timeUs = 0;
        MessageLog::Event event = MessageLog::Event::Incoming;
        std::string_view content;
    };

}

template<typename T>
static bool parseNumber(std::string_view str, T& out) {
    auto result = std::from_chars(str.data(), str.data() + str.size(), out);
    return result.ec == std::errc() && result.ptr == str.data() + str.size();
}

//! Splits a recorded log into its entries, tolerating a truncated last entry
static std::vector<LogEntry> parseLog(std::string_view data) {
    std::vector<LogEntry> entries;

    while (!data.empty()) {
        LogEntry entry;
        int_t contentLength = -1;
        bool headerComplete = false;

        while (true) {
            auto lineEnd = data.find("\r\n");
            if (lineEnd == std::string_view::npos)
                break;
            auto line = data.substr(0, lineEnd);
            data = data.substr(lineEnd + 2);

            if (line.empty()) {
                headerComplete = true;
                break;
            }
            if (line.starts_with(LENGTH_HEADER)) {
                if (!parseNumber(line.substr(LENGTH_HEADER.length()), contentLength))
                    contentLength = -1;
            } else if (line.starts_with(TIME_HEADER)) {
                parseNumber(line.substr(TIME_HEADER.length()), entry.timeUs);
            } else if (line.starts_with(EVENT_HEADER)) {
                auto name = line.substr(EVENT_HEADER.length());
                for (auto event : { MessageLog::Event::Incoming, MessageLog::Event::Outgoing, MessageLog::Event::Handled }) {
                    if (name == eventName(event))
                        entry.event = event;
                }
            }
        }

        if (!headerComplete || contentLength < 0 || (int_t)data.size() < contentLength) {
            dbgln("Message log ends with an incomplete entry, it was likely cut short by a crash");
            break;
        }

        entry.content = data.substr(0, contentLength);
        data = data.substr(contentLength);
        entries.push_back(entry);
    }

    return entries;
}

void replayLog(const std::filesystem::path& logFile, bool realtime) {
    std::string logData = readFile(logFile);
    auto entries = parseLog(logData);

    int_t messageCount = 0;
    for (const auto& entry : entries) {
        if (entry.event == MessageLog::Event::Incoming)
            messageCount += 1;
    }
    dbgln("Replaying {} messages from {}", messageCount, logFile.string());

    Server server;
    auto startTime = std::chrono::steady_clock::now();
    int_t messageIndex = 0;

    for (const auto& entry : entries) {
        if (entry.event != MessageLog::Event::Incoming)
            continue;
        if (realtime)
            std::this_thread::sleep_until(startTime + std::chrono::microseconds(entry.timeUs));

        messageIndex += 1;
        dbgln("Replaying message {} of {}", messageIndex, messageCount);

        auto header = std::format("{}{}\r\n\r\n", LENGTH_HEADER, entry.content.size());
        for (char c : header)
            server.receiverChacacter(c);
        for (char c : entry.content)
            server.receiverChacacter(c);

        // The responses are not checked, the point of a replay is to reach a crash
        server.outputBuffer.clear();
    }

    dbgln("Replay finished");
}

}
