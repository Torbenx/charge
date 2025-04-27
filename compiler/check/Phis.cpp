#include <check/Phis.h>

#include <check/SatSolver.h>

#include <ReverseMemberPointer.h>

namespace check {

// ------------------------------ Links -----------------------------

Phis* Phis::Links::phis() {
    return ReverseMemberPointer<&Phis::links>::reverse(this);
}

std::string Phis::Links::formatElement(Solver& solver, int_t setId, int_t index) {
    BlockId block = phis()->blockFromId(setId);
    const Phi& phi = phis()->get(block);
    std::string blockName = phis()->formatBlockName(solver, block);
    if (index == (int_t)phi.parents.size())
        return "inactive(" + blockName + ")";
    else
        return blockName + "->" + solver.formatBlockName(phi.parents[index]);
}

void Phis::Links::onElementActivated(Solver& solver, int_t setId, int_t index) {
    BlockId block = phis()->blockFromId(setId);
    Phi& phi = phis()->get(block);
    if (index == (int_t)phi.parents.size())
        return; // The block was explicitly assigned inactive

    BooleanValue negatedCondition = elementInactiveLiteral(solver, block.blockId, index);
    solver.implicationAssignTrue(negatedCondition, solver.blockActiveLiteral(phi.parents[index]));

    for (int_t locIndex = 0; locIndex < phi.locations.size(); locIndex++) {
        phis()->propagteActiveLink(solver, block, phi.locations.keyAt(locIndex), index, phi.locations.at(locIndex));
    }
}

uint32_t Phis::Links::labelOfElement(Solver&, int_t setId, int_t index) {
    int_t maxParentCount = 16;
    return setId * maxParentCount + index;
}

// ------------------------------ Phis ------------------------------

std::strong_ordering Phis::LocationCache::compare(Solver& solver, MemoryLocation a, BlockId, int_t, const std::pair<MemoryLocation, CachedValues>& pair) {
    return solver.compare(a, pair.first);
}
uint32_t Phis::LocationCache::makeNode(Solver& solver, MemoryLocation location, BlockId block, int_t parentCount, TreeLabel label) {
    Value load = solver.defineLoad(location, { block, 0 });
    auto equalityCache = std::make_unique<std::optional<BooleanValue>[]>(parentCount);
    return Base::makeNode(label, std::make_pair(location, CachedValues { load, std::move(equalityCache) }));
}

Phis::Phis(Solver& solver, uint64_t baseLabel)
    : CodeBlockTheory(solver)
    , links(solver, baseLabel) { }

BlockId Phis::newPhi(Solver& solver, uint32_t label, std::vector<BlockId> parents) {
    BlockId block = blockFromId(blocks.size());

    int_t linkCount = parents.size();
    blocks.push_back({ label, std::move(parents) });

    // The extra (+1) variable in the set is used as the block active variable.
    // It is negated such that if the block is inactive all links inactive as well.
    int_t setId = links.newSet(solver, linkCount + 1);
    VERIFY(setId == (int_t)block.blockId);

    return block;
}

std::string Phis::formatBlockName(Solver&, BlockId block) {
    return "phi" + std::to_string(block.blockId);
}

std::string Phis::formatCodePosition(Solver& solver, CodePosition position) {
    return formatBlockName(solver, position.block);
}

uint64_t Phis::labelOfBlock(Solver&, BlockId block) {
    return get(block).label;
}

Value Phis::loadAtEndOfBlock(Solver& solver, MemoryLocation location, BlockId block) {
    Phi& phi = get(block);
    int_t oldSize = phi.locations.size();
    auto& cache = phi.locations.at(phi.locations.get(solver, location, block, phi.parents.size()));
    bool isNewLocation = phi.locations.size() == oldSize;

    if (isNewLocation && links.hasActiveElement(block.blockId)) {
        propagteActiveLink(solver, block, location, links.activeElement(block.blockId), cache);
    }

    return cache.load;
}

void Phis::propagteActiveLink(Solver& solver, BlockId block, MemoryLocation location, int_t link, CachedValues& cache) {
    std::optional<BooleanValue>& equality = cache.equalities[link];
    if (!equality.has_value()) {
        Value v = solver.loadAtEndOfBlock(location, get(block).parents[link]);
        equality = solver.equality(cache.load, v);
    }

    BooleanValue negatedCondition = linkInactiveLiteral(solver, block, link);
    solver.implicationAssignTrue(negatedCondition, equality.value());
}

Value Phis::loadAtPosition(Solver& solver, MemoryLocation location, CodePosition position) {
    return loadAtEndOfBlock(solver, location, position.block);
}

BooleanValue Phis::blockActiveLiteral(Solver& solver, BlockId block) {
    return links.elementInactiveLiteral(solver, block.blockId, get(block).parents.size());
}

}