#pragma once

#include <glue/Context.h>

namespace parse {

using ParseState = glue::Context;

struct ErrorHandler {
    virtual void invalidCharaceter() { }
    virtual void invalidToken(Token, State, ScopeKind*, ParseState&) { }
    virtual ~ErrorHandler() = default;
};

void parseImpl(const char* position, ParseState& state, ErrorHandler* errorHandler);

}