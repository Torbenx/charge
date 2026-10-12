#pragma once

#include <parse/TokenBuffer.h>
#include <parse/api.h>
#include <parse/parse_gen.h>

#include <array>

namespace sema {
struct Context;
}

namespace parse {

struct RecoveryElement;

inline constexpr int_t SCOPE_BUFFER_SIZE = 1024;

struct ScopeBuffer {
    struct alignas(SCOPE_BUFFER_SIZE) Storage {
        std::array<ScopeKind, SCOPE_BUFFER_SIZE> data;
    };
    static_assert(sizeof(Storage) == alignof(Storage));

    static size_t toIndex(ScopeKind* position) {
        return (uintptr_t)position & (SCOPE_BUFFER_SIZE - 1);
    }

    ScopeKind* data() { return storage->data.data(); }
    const ScopeKind* data() const { return storage->data.data(); }
    std::vector<ScopeKind> save(const ScopeKind* position) const {
        VERIFY(position + 1 - data() <= SCOPE_BUFFER_SIZE);
        return { data(), position + 1 };
    }
    ScopeKind* restore(const std::vector<ScopeKind>& vec) {
        VERIFY(vec.size() <= SCOPE_BUFFER_SIZE);
        return std::copy(vec.begin(), vec.end(), data()) - 1;
    }

    ScopeBuffer()
        : storage(std::make_unique_for_overwrite<Storage>()) { }
    std::unique_ptr<Storage> storage;
};

inline constexpr int_t ARGUMENT_BUFFER_SIZE = 1024;
inline constexpr int_t ARGUMENT_BUFFER_SIZE_IN_BYTES = ARGUMENT_BUFFER_SIZE * sizeof(Word);

struct ArgumentBuffer {
    struct alignas(ARGUMENT_BUFFER_SIZE_IN_BYTES) Storage {
        std::array<Word, ARGUMENT_BUFFER_SIZE> data;
    };
    static_assert(sizeof(Storage) == alignof(Storage));

    static size_t toIndex(Word* position) {
        return ((uintptr_t)position & (ARGUMENT_BUFFER_SIZE_IN_BYTES - 1)) / sizeof(Word);
    }

    Word* data() { return storage->data.data(); }
    const Word* data() const { return storage->data.data(); }
    std::vector<Word> save(const Word* position) const {
        VERIFY(position + 1 - data() <= ARGUMENT_BUFFER_SIZE);
        return { data(), position + 1 };
    }
    Word* restore(const std::vector<Word>& vec) {
        VERIFY(vec.size() <= ARGUMENT_BUFFER_SIZE);
        return std::copy(vec.begin(), vec.end(), data()) - 1;
    }

    ArgumentBuffer()
        : storage(std::make_unique_for_overwrite<Storage>()) { }
    std::unique_ptr<Storage> storage;
};

struct SimpleTokenInfo {
    TaggedSourceLocation<TokenKind> m_fields;
    SimpleTokenInfo(TokenKind kind, SourceLocation location)
        : m_fields(kind, location) { }
    TokenKind kind() const { return m_fields.tag(); }
    void setKind(TokenKind kind) { m_fields.setTag(kind); }
};

struct SimpleTokenBuffer {
    PageBumpAllocator<SimpleTokenInfo> tokens;
    PageBumpAllocator<LineInfo> lines;
    PageBumpAllocator<WhitespaceInfo> whitespace;
    padded_string_view source;
    SourceLocation lastLineStartLocation;
    const char* lastLineStartPosition = nullptr;

    SimpleTokenBuffer(padded_string_view source)
        : source(source) {
        reset();
    }

    TokenHandle currentToken() const { return { (uint32_t)tokens.size() }; }
    void reset() {
        tokens.clear();
        lines.clear();
        addLine(source.begin());
    }

    void addLine(const char* position) {
        lines.push_back({ position });
        lastLineStartLocation = SourceLocation(lastLineStartLocation.fileId(), lines.size() - 1, 0);
        lastLineStartPosition = position;
    }
};

struct SimpleOutput {
    SimpleTokenBuffer tokenBuffer;

    SimpleOutput(padded_string_view source)
        : tokenBuffer(source) { }

    void reset() { tokenBuffer.reset(); }
};

struct NoTokenBuffer {
    TokenHandle currentToken() const { return {}; }
    void reset() { }
};
struct NoOutput {
    NoTokenBuffer tokenBuffer;

    void reset() { tokenBuffer.reset(); }
};

[[nodiscard]] const char* advanceToToken(const char* input);
LexerToken lexToken(const char*&);

struct SimpleParser;

struct Parser {
    Parser(padded_string_view source);

    ReturnStatus status() const { return m_state.status; }
    bool done() const { return status() == ReturnStatus::EOS; }
    bool error() const {
        return status() == ReturnStatus::UnhandledCase || status() == ReturnStatus::ScopeError;
    }

    State state() const { return m_state.state; }
    int_t parsedTokens() const { return m_state.parsedTokens; }

    const char* sourcePosition() const { return m_state.sourcePosition; }
    SourceLocation location(sema::Context&) const;

    ScopeKind topScope() const { return m_state.scopePosition[0]; }
    std::span<const ScopeKind> scopes() const {
        return { scopeBuffer.data(), m_state.scopePosition + 1 };
    }

    void advanceToToken(sema::Context&);
    LexerToken skipToken(sema::Context&);
    ReturnStatus parse(sema::Context&, int_t tokenLimit = -1);
    ReturnStatus apply(sema::Context&, RecoveryElement);
    ReturnStatus apply(sema::Context&, const RecoveryInstructions&);

private:
    struct InternalState {
        ReturnStatus status = ReturnStatus::Ready;
        State state = State::Start;
        uint32_t parsedTokens = 0;
        TokenHandle declarationBegin = {};
        Word savedArgumentName = {};
        const char* sourcePosition = nullptr;
        ScopeKind* scopePosition = nullptr;
        Word* argumentPosition = nullptr;
    };

    template<typename ParseOutput>
    static InternalState parseImpl(const InternalState&, ParseOutput&, int_t tokenLimit);

    ScopeBuffer scopeBuffer;
    ArgumentBuffer argumentBuffer;
    InternalState m_state;

    friend SimpleParser;
};

struct SimpleParser {
    SimpleParser();
    SimpleParser(padded_string_view source);

    ReturnStatus status() const { return m_state.status; }
    bool done() const { return status() == ReturnStatus::EOS; }
    bool error() const {
        return status() == ReturnStatus::UnhandledCase || status() == ReturnStatus::ScopeError;
    }

    State state() const { return m_state.state; }
    int_t parsedTokens() const { return m_state.parsedTokens; }

    const char* sourcePosition() const { return m_state.sourcePosition; }
    void setSourcePosition(const char* pos) { m_state.sourcePosition = pos; }

    ScopeKind topScope() const { return m_state.scopePosition[0]; }
    std::span<const ScopeKind> scopes() const {
        return { scopeBuffer.data(), m_state.scopePosition + 1 };
    }

    void pushScope(ScopeKind);
    ScopeKind popScope();

    void advanceToToken(SimpleOutput&);
    LexerToken skipToken(SimpleOutput&);
    ReturnStatus parse(SimpleOutput&, int_t tokenLimit = -1);
    ReturnStatus apply(SimpleOutput&, RecoveryElement);
    ReturnStatus apply(SimpleOutput&, const RecoveryInstructions&);
    void advanceToToken(const NoOutput&);
    LexerToken skipToken(const NoOutput&);
    ReturnStatus parse(const NoOutput&, int_t tokenLimit = -1);
    ReturnStatus apply(const NoOutput&, RecoveryElement);
    ReturnStatus apply(const NoOutput&, const RecoveryInstructions&);

    SavedParserState save() const;
    void restore(const SavedParserState&);
    static SavedParserState saveStateOf(const Parser&);
    void copyState(const Parser&);

private:
    struct InternalState {
        ReturnStatus status = ReturnStatus::Ready;
        State state = State::Start;
        uint32_t parsedTokens = 0;
        const char* sourcePosition = nullptr;
        ScopeKind* scopePosition = nullptr;
    };

    ScopeBuffer scopeBuffer;
    InternalState m_state;
};

}