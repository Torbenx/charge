#pragma once

#include <types.h>

namespace sema {
struct Context;
}

namespace parse {

enum class LexerToken : uint8_t;
enum class State : uint8_t;
enum class ScopeKind : uint8_t;
struct SimpleOutput;

enum class ReturnStatus : uint8_t {
    Ready,
    EOS,
    UnhandledCase,
    ScopeError,
};

struct SavedParserState {
    ReturnStatus status;
    State state;
    uint32_t parsedTokens = 0;
    const char* sourcePosition = nullptr;
    std::vector<ScopeKind> scopeBuffer;

    bool operator==(const SavedParserState&) const = default;
};

struct Error {
    static Error make(SavedParserState preRecoveryState);

    SavedParserState preRecoveryState;
    SavedParserState errorState;
    LexerToken errorToken;
};

struct RecoveryInstructions {
    uint32_t skipTokens = 0;
    std::vector<LexerToken> insertTokens = {};

    bool operator==(const RecoveryInstructions&) const = default;
};

struct RecoveredError : Error {
    RecoveryInstructions recovery;
    bool unanimousAndIsolated = false;
};

struct ParseException : std::exception {
    ParseException(Error error, std::string message)
        : m_error(std::move(error)), m_message(std::move(message)) { }
    const char* what() const noexcept override;
    const Error& error() const { return m_error; }

private:
    Error m_error;
    std::string m_message;
};

[[nodiscard]] std::optional<Error> tryParse(sema::Context&);
void parseOrThrow(sema::Context&);
std::vector<RecoveredError> parseAndRecover(sema::Context&);

std::string formatInternalErrorMessage(const Error&, sema::Context&);
std::string formatInternalErrorMessage(const Error&);

}