#include <check/StoreBlocks.h>

#include <check/SatSolver.h>

namespace check {

StoreBlocks::StoreBlocks(Solver& solver)
    : CodeBlockTheory(solver) { }

std::string StoreBlocks::formatBlockName(Solver&, BlockId block) {
    return "stores" + std::to_string(block.blockId);
}

std::string StoreBlocks::formatCodePosition(Solver& solver, CodePosition position) {
    return formatBlockName(solver, position.block) + ":" + std::to_string(position.position);
}

uint64_t StoreBlocks::labelOfBlock(Solver&, BlockId block) {
    return get(block).label;
}

Value StoreBlocks::loadAtEndOfBlock(Solver& solver, MemoryLocation location, BlockId block) {
    return loadAtPosition(solver, location, { block, (uint32_t)(get(block).stores.size() - 1) });
}

Value StoreBlocks::loadAtPosition(Solver& solver, MemoryLocation location, CodePosition position) {
    Block& block = get(position.block);

    for (int_t storeIndex = position.position; storeIndex >= 0; storeIndex--) {
        Store& store = block.stores[position.position];

        if (solver.loadedKind(store.location) != solver.loadedKind(location))
            continue;
        if (store.location == location)
            return store.value;

        VERIFY_NOT_REACHED();
    }

    return solver.loadAtEndOfBlock(location, block.parent);
}

BlockId StoreBlocks::newBlock(uint32_t label, BlockId parent) {
    BlockId result { (uint32_t)theoryId(), (uint32_t)blocks.size() };
    blocks.push_back({ parent, label });
    return result;
}

void StoreBlocks::appendStore(BlockId block, MemoryLocation location, Value value) {
    get(block).stores.push_back({ location, value });
}

StoreBlocks::Block& StoreBlocks::get(BlockId b) { return blocks[b.blockId]; }

}