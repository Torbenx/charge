#pragma once

#include <sema/Context.h>

namespace parse {

using ParseState = sema::Context;

struct ErrorHandler {
    virtual void invalidCharaceter() { }
    virtual void invalidToken(LexerToken, State, ScopeKind*, ParseState&) { }
    virtual ~ErrorHandler() = default;
};

void parseImpl(const char* position, ParseState& state, ErrorHandler* errorHandler);

}