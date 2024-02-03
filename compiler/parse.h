#pragma once

#include "parse_gen.h"

#include <vector>

namespace parse {

enum class NodeKind {
#define NODE(kind, type, prec) kind,

#include "nodes.inc"
};
std::string_view nameString(NodeKind);

struct Node {
    NodeKind kind;
    uint32_t begin;
    uint32_t end;
};

struct ErrorHandler {
    virtual void invalidCharaceter() { }
    virtual void invalidToken(State, Token) { }
    virtual ~ErrorHandler() = default;
};

std::vector<Node> parseExpression(const char* sourceBufferBegin, const char* position, ErrorHandler* errorHandler);

}