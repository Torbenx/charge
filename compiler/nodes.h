#pragma once

#include "types.h"
#include "WordTable.h"

#include <vector>

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

std::vector<Node> parse(std::string_view);

constexpr auto words = ConstWordStringTable(
    // parser
    keyword("if"), keyword("elif"), keyword("else"), keyword("namespace"), keyword("struct"), keyword("object"), keyword("fn"),
    keyword("with"), keyword("template"), keyword("var"), keyword("let"), keyword("inout"), keyword("out"),
    keyword("static"), keyword("return"), keyword("has"), keyword("as"), keyword("in"),
    // sema
    "type", "type_template_literal", "function_literal", "function_template_literal", "void");