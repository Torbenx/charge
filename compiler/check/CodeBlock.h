#pragma once

#include <check/Value.h>

namespace check {

struct CodeBlock {
    enum class Kind : uint8_t {
        Entry,
        Store,
        Phi,
    };

    CodeBlock(Kind kind)
        : kind(kind) { }

    Kind kind;
};

}