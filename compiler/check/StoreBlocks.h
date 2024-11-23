#pragma once

#include <check/CodeBlockTheory.h>

namespace check {

struct StoreBlocks : CodeBlockTheory {
    StoreBlocks(Solver&);

    std::string formatBlockName(Solver&, BlockId) override;
    std::string formatCodePosition(Solver&, CodePosition) override;

    uint64_t labelOf(Solver&, BlockId) override;

    Value loadAtEndOfBlock(Solver&, MemoryLocation, BlockId) override;
    Value loadAtPosition(Solver&, MemoryLocation, CodePosition) override;

    BlockId newBlock(uint32_t label, BlockId parent);
    void appendStore(BlockId, MemoryLocation, Value);

private:
    struct Store {
        MemoryLocation location;
        Value value;
    };
    struct Block {
        BlockId parent;
        uint32_t label;
        std::vector<Store> stores = {};
    };

    Block& get(BlockId);

    std::vector<Block> blocks;
};

}