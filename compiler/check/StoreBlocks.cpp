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

Value StoreBlocks::loadAtPosition(Solver& solver, MemoryLocation location, CodePosition initialPosition) {
    BlockId blockId = initialPosition.block;
    Block& block = get(blockId);

    for (int_t storeIndex = initialPosition.position; storeIndex >= 0; storeIndex--) {
        Store& store = block.stores[storeIndex];

        // TODO: Should the case where of locations are equal handled in possibleOrderings() and equality()?
        if (store.location == location)
            return store.value;
        auto orderings = solver.possibleOrderings(store.location, location);
        // Both types should be primitives and thus not contain other types
        VERIFY(!orderings.test(std::partial_ordering::less));
        VERIFY(!orderings.test(std::partial_ordering::greater));
        if (orderings.count() == 1) {
            if (orderings.test(std::partial_ordering::equivalent))
                return store.value;
            else
                continue;
        }

        CodePosition currentPosition = { blockId, (uint32_t)storeIndex };
        Value currentValue = solver.defineLoad(location, currentPosition);
        Value previousValue = storeIndex > 0
            ? loadAtPosition(solver, location, { blockId, uint32_t(storeIndex - 1) }) // Awkward recursive call
            : solver.loadAtEndOfBlock(location, block.parent);
        BooleanValue condition = solver.equality(store.location, location);
        solver.addClause({ solver.negate(condition), solver.equality(currentValue, store.value) });
        solver.addClause({ condition, solver.equality(currentValue, previousValue) });
        return currentValue;
    }

    return solver.loadAtEndOfBlock(location, block.parent);
}

BlockId StoreBlocks::newBlock(Solver& solver, uint32_t label, BlockId parent) {
    BlockId result { (uint32_t)theoryId(), (uint32_t)blocks.size() };
    // This block is active if and only if the parent block is.
    blocks.push_back({ parent, label, solver.blockActiveLiteral(parent) });
    return result;
}

void StoreBlocks::appendStore(Solver&, BlockId block, MemoryLocation location, Value value) {
    get(block).stores.push_back({ location, value });
}

BooleanValue StoreBlocks::blockActiveLiteral(Solver&, BlockId block) {
    return get(block).blockActiveLiteral;
}

StoreBlocks::Block& StoreBlocks::get(BlockId b) { return blocks[b.blockId]; }

}