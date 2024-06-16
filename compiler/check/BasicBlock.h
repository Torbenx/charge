#pragma once

#include <types.h>
#include <check/Value.h>

namespace check {

struct BasicBlock {
    enum class Kind : uint8_t {
        Branch,
        Stores,
        Call,
    };

    Kind kind;
    BasicBlock* next = nullptr;
    BasicBlock* immediateDominator = nullptr;
};

struct Branch : BasicBlock {
    BooleanValue condition;
    BasicBlock* ifTrue = nullptr;
};

struct StoreBlock : BasicBlock {
    struct Store {
        Value location;
        Value value;
    };
    uint32_t count;
    Store ops[];
};

struct Call : BasicBlock {
    // ? target
    uint32_t argumentCount;
    Value arguments[];
};

}