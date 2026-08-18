#pragma once

#include <types.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

namespace server {

//! Records the incoming messages of a language server session for later replay
struct MessageLog {
    //! Recording stops once the log has grown to this size
    static constexpr int_t SIZE_LIMIT = 64 * 1024 * 1024;

    enum class Event {
        //! A complete message received from the client
        Incoming,
        //! A message was sent back to the client, the content is not recorded
        Outgoing,
        //! The preceding incoming message was handled without crashing
        Handled,
    };

    //! Creates a log if CHARGE_LSP_RECORD is set, returns nullptr otherwise
    static std::unique_ptr<MessageLog> createFromEnvironment();

    explicit MessageLog(const std::filesystem::path& logFile);

    void record(Event event, std::string_view content = {});

    std::ofstream stream;
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    int_t writtenSize = 0;
    bool limitReached = false;
};

//! Feeds a recorded log back into a fresh server
/*!
In realtime mode the messages are replayed with the delays that were recorded,
otherwise every message is sent as soon as the previous one was handled.
*/
void replayLog(const std::filesystem::path& logFile, bool realtime);

}
