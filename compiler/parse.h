#pragma once

#include "WordTable.h"
#include "parse_gen.h"

#include <vector>

inline constexpr ConstWordStringTable words(
    keyword("if"), keyword("elif"), keyword("else"), keyword("match"), keyword("for"), keyword("while"), keyword("do"),
    keyword("return"), keyword("break"), keyword("continue"), keyword("loop"), keyword("guard"), keyword("try"), keyword("catch"),
    keyword("with"), keyword("analysis"), keyword("assert"),
    keyword("namespace"), keyword("struct"), keyword("trait"), keyword("object"), keyword("fn"), keyword("static"),
    keyword("template"),
    keyword("var"), keyword("let"), keyword("in"), keyword("inout"), keyword("out"), keyword("forward"), keyword("assign"));

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