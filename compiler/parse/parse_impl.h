#pragma once

#include <parse/parse_gen.h>
#include <sema/Context.h>

namespace parse {

struct Parser;

inline constexpr int_t SCOPE_BUFFER_SIZE = 1024;

struct ScopeBuffer {
    static size_t toIndex(ScopeKind* position) {
        return (uintptr_t)position & (SCOPE_BUFFER_SIZE - 1);
    }

    std::vector<ScopeKind> save(ScopeKind* position) const {
        VERIFY(position + 1 - buffer < SCOPE_BUFFER_SIZE);
        return { buffer, position + 1 };
    }
    ScopeKind* restore(const std::vector<ScopeKind>& vec) {
        VERIFY(vec.size() <= SCOPE_BUFFER_SIZE);
        return std::copy(vec.begin(), vec.end(), buffer) - 1;
    }

    ScopeKind* buffer;
    ScopeBuffer()
        : buffer((ScopeKind*)::operator new(SCOPE_BUFFER_SIZE, std::align_val_t(SCOPE_BUFFER_SIZE))) { }
    ~ScopeBuffer() {
        ::operator delete(buffer, SCOPE_BUFFER_SIZE, std::align_val_t(SCOPE_BUFFER_SIZE));
    }
};

inline constexpr int_t ARGUMENT_BUFFER_SIZE = 1024;
inline constexpr int_t ARGUMENT_BUFFER_SIZE_IN_BYTES = ARGUMENT_BUFFER_SIZE * sizeof(Word);

struct ArgumentBuffer {
    static size_t toIndex(Word* position) {
        return ((uintptr_t)position & (ARGUMENT_BUFFER_SIZE_IN_BYTES - 1)) / sizeof(Word);
    }

    std::vector<Word> save(Word* position) const {
        VERIFY(position + 1 - buffer < ARGUMENT_BUFFER_SIZE);
        return { buffer, position + 1 };
    }
    Word* restore(const std::vector<Word>& vec) {
        VERIFY(vec.size() <= ARGUMENT_BUFFER_SIZE);
        return std::copy(vec.begin(), vec.end(), buffer) - 1;
    }

    Word* buffer;
    ArgumentBuffer()
        : buffer((Word*)::operator new(ARGUMENT_BUFFER_SIZE_IN_BYTES, std::align_val_t(ARGUMENT_BUFFER_SIZE_IN_BYTES))) { }
    ~ArgumentBuffer() {
        ::operator delete(buffer, ARGUMENT_BUFFER_SIZE_IN_BYTES, std::align_val_t(ARGUMENT_BUFFER_SIZE_IN_BYTES));
    }
};

struct SimpleTokenInfo {
    TokenKind m_kind;
    SimpleTokenInfo(TokenKind kind)
        : m_kind(kind) { }
    TokenKind kind() const { return m_kind; }
    void setKind(TokenKind kind) { m_kind = kind; }
};

struct SimpleTokenBuffer {
    std::vector<SimpleTokenInfo> tokens;

    TokenHandle currentToken() const { return { (uint32_t)tokens.size() }; }
};

struct SimpleOutput {
    SimpleTokenBuffer tokenBuffer;
};

enum class ReturnStatus : uint8_t {
    EOS,
    UnhandledCase,
    ScopeError,
};

struct StateMachineState {
    ReturnStatus status = ReturnStatus::UnhandledCase;
    State state = State::Error;
    State continueState = State::Error;
    const char* sourcePosition = nullptr;
    ScopeKind* scopePosition = nullptr;
    Word* argumentPosition = nullptr;
};

struct SavedState {
    ReturnStatus status = ReturnStatus::UnhandledCase;
    State state = State::Error;
    State continueState = State::Error;
    const char* sourcePosition = nullptr;
    std::vector<ScopeKind> scopeBuffer;
    std::vector<Word> argumentBuffer;
};

struct Parser {
    Parser(const char* sourcePosition);

    ReturnStatus status() const { return m_state.status; }
    bool checkFinalState() const;
    bool done() const { return status() == ReturnStatus::EOS && checkFinalState(); }

    State state() const { return m_state.state; }
    void setState(State state) {
        m_state.state = state;
        m_state.continueState = state;
    }

    const char* sourcePosition() const { return m_state.sourcePosition; }
    void setSourcePosition(const char* pos) { m_state.sourcePosition = pos; }

    LexerToken lexToken();

    void parse(SimpleOutput& output);
    void parse(sema::Context& output);

    void pushScope(ScopeKind scope) {
        auto index = ScopeBuffer::toIndex(m_state.scopePosition);
        VERIFY(index + 1 < (size_t)SCOPE_BUFFER_SIZE);
        m_state.scopePosition += 1;
        m_state.scopePosition[0] = scope;
    }

    ScopeKind popScope() {
        auto index = ScopeBuffer::toIndex(m_state.scopePosition);
        VERIFY(index > 0);
        ScopeKind ret = m_state.scopePosition[0];
        m_state.scopePosition -= 1;
        return ret;
    }

    ScopeKind topScope() const { return m_state.scopePosition[0]; }

    std::span<const ScopeKind> scopes() const {
        return { scopeBuffer.buffer, m_state.scopePosition };
    }

    SavedState save() const {
        return {
            m_state.status,
            m_state.state,
            m_state.continueState,
            m_state.sourcePosition,
            scopeBuffer.save(m_state.scopePosition),
            argumentBuffer.save(m_state.argumentPosition),
        };
    }
    void restore(const SavedState& in) {
        m_state = {
            in.status,
            in.state,
            in.continueState,
            in.sourcePosition,
            scopeBuffer.restore(in.scopeBuffer),
            argumentBuffer.restore(in.argumentBuffer)
        };
    }

private:
    ScopeBuffer scopeBuffer;
    ArgumentBuffer argumentBuffer;
    StateMachineState m_state;
};

}