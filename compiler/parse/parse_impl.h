#pragma once

#include <parse/parse_gen.h>
#include <sema/Context.h>

namespace parse {

inline constexpr int_t SCOPE_BUFFER_SIZE = 1024;

struct ScopeBuffer {
    static size_t toIndex(ScopeKind* position) {
        return (uintptr_t)position & (SCOPE_BUFFER_SIZE - 1);
    }

    ScopeKind* buffer;
    ScopeBuffer()
        : buffer((ScopeKind*)::operator new(SCOPE_BUFFER_SIZE, std::align_val_t(SCOPE_BUFFER_SIZE))) { }
    ~ScopeBuffer() {
        ::operator delete(buffer, SCOPE_BUFFER_SIZE, std::align_val_t(SCOPE_BUFFER_SIZE));
    }
};

using ParseOutput = sema::Context;

inline constexpr int_t ARGUMENT_BUFFER_SIZE = 1024;
inline constexpr int_t ARGUMENT_BUFFER_SIZE_IN_BYTES = ARGUMENT_BUFFER_SIZE * sizeof(Word);

struct ArgumentBuffer {
    static size_t toIndex(Word* position) {
        return ((uintptr_t)position & (ARGUMENT_BUFFER_SIZE_IN_BYTES - 1)) / sizeof(Word);
    }
    Word* buffer;
    ArgumentBuffer()
        : buffer((Word*)::operator new(ARGUMENT_BUFFER_SIZE_IN_BYTES, std::align_val_t(ARGUMENT_BUFFER_SIZE_IN_BYTES))) { }
    ~ArgumentBuffer() {
        ::operator delete(buffer, ARGUMENT_BUFFER_SIZE_IN_BYTES, std::align_val_t(ARGUMENT_BUFFER_SIZE_IN_BYTES));
    }
};

struct StateMachineState {
    State state = State::Error;
    const char* sourcePosition = nullptr;
    ScopeKind* scopePosition = nullptr;
    Word* argumentPosition = nullptr;
};

struct ErrorHandler {
    virtual void invalidCharacter() { }
    virtual void invalidToken(LexerToken, State, ScopeKind*, ParseOutput&) { }
    virtual ~ErrorHandler() = default;
};

void parseImpl(const char* position, ParseOutput& output, ErrorHandler* errorHandler);

}