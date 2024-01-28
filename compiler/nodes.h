#pragma once

#include "types.h"

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