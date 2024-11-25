#include <check/Phis.h>

#include <check/SatSolver.h>

namespace check {

OneOfTheory::OneOfTheory(Solver& solver)
    : SimpleBooleanTheory(solver), ReasonTheory(solver, true) {
    nodes.push_back({ 0 });
}

std::string OneOfTheory::formatPositiveLiteral(Solver& solver, int_t varId) {
    int_t nodeId = findNodeForVar(varId);
    return formatIndex(solver, nodeId, varId - nodes[nodeId].firstVarId);
}

std::string OneOfTheory::formatNegativeLiteral(Solver& solver, int_t varId) {
    return "!" + formatPositiveLiteral(solver, varId);
}

uint64_t OneOfTheory::labelOfValue(Solver& solver, Value v) {
    BooleanValue lit { v };
    int_t varId = variableId(lit);
    int_t nodeId = findNodeForVar(varId);
    return labelOfIndex(solver, nodeId, varId - nodes[nodeId].firstVarId, isPositive(lit));
}

int_t OneOfTheory::newNode(Solver& solver, int_t varCount) {
    VERIFY((int_t)nodes.back().firstVarId == variableCount());
    int_t nodeId = nodeCount();
    std::vector<BooleanValue> oneMustBeActiveClause;
    oneMustBeActiveClause.reserve(varCount);
    for (int_t i = 0; i < varCount; i++)
        oneMustBeActiveClause.push_back(positiveLiteral(newVariable()));
    nodes.push_back({ (uint32_t)variableCount() });
    solver.addClause(std::move(oneMustBeActiveClause));
    return nodeId;
}

int_t OneOfTheory::findNodeForVar(int_t varId) {
    auto rit = std::partition_point(
        nodes.rbegin(), nodes.rend(),
        [=](const NodeInfo& info) {
            return (int_t)info.firstVarId > varId;
        });
    return &*rit - nodes.data();
}

void OneOfTheory::propagateFalseAssignment(Solver& solver, BooleanValue value) {
    if (isPositive(value))
        return;

    int_t activeVarId = variableId(value);
    int_t nodeId = findNodeForVar(activeVarId);
    auto& node = nodes[nodeId];
    VERIFY(node.activeIndex == INVALID_ACTIVE_INDEX);
    node.activeIndex = activeVarId - node.firstVarId;
    onIndexActivated(solver, nodeId, activeVarId - node.firstVarId);

    int_t endVarId = nodes[nodeId + 1].firstVarId;
    for (int_t varId = node.firstVarId; varId < endVarId; varId++) {
        if (varId == activeVarId)
            continue;

        solver.assignTrue(
            negativeLiteral(varId),
            makeOtherVarActiveReason(nodeId, activeVarId, varId));
    }
}

void OneOfTheory::unapplyFalseAssignment(Solver&, BooleanValue value) {
    if (isPositive(value))
        return;

    int_t activeVarId = variableId(value);
    int_t nodeId = findNodeForVar(activeVarId);
    VERIFY(nodes[nodeId].activeIndex == activeVarId - nodes[nodeId].firstVarId);
    nodes[nodeId].activeIndex = INVALID_ACTIVE_INDEX;
}

Reason OneOfTheory::makeOtherVarActiveReason(int_t nodeId, int_t activeVarId, int_t inactiveVarId) {
    return Reason { (uint32_t)ReasonTheory::theoryId(), (uint32_t)nodeId, (uint32_t)activeVarId, (uint32_t)inactiveVarId };
}

int_t OneOfTheory::reasonNodeId(const Reason& reason) { return reason.data0; }
int_t OneOfTheory::reasonActiveVarId(const Reason& reason) { return reason.data1; }
int_t OneOfTheory::reasonInactiveVarId(const Reason& reason) { return reason.data2; }

bool OneOfTheory::testReason(Solver& solver, const Reason& reason) {
    return assignedPositive(solver, reasonActiveVarId(reason));
}

ReasonTheory::ClauseAndIndex OneOfTheory::reasonToClause(Solver& solver, const Reason& reason) {
    auto& clause = solver.scratchClause();
    clause.push_back(negativeLiteral(reasonActiveVarId(reason)));
    clause.push_back(negativeLiteral(reasonInactiveVarId(reason)));
    return { clause, 1 };
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
    : CodeBlockTheory(solver), OneOfTheory(solver), implications(solver), baseLabel(baseLabel) { }

std::string Phis::formatIndex(Solver& solver, int_t nodeId, int_t index) {
    BlockId block { (uint32_t)CodeBlockTheory::theoryId(), (uint32_t)nodeId };
    return formatBlockName(solver, block) + "->" + solver.formatBlockName(get(block).parents[index]);
}

uint64_t Phis::labelOfIndex(Solver&, int_t nodeId, int_t index, bool positive) {
    int_t maxParentCount = 16;
    return baseLabel + (nodeId * maxParentCount + index) * 2 + positive;
}

BlockId Phis::newPhi(Solver& solver, uint32_t label, std::vector<BlockId> parents) {
    BlockId block { (uint32_t)CodeBlockTheory::theoryId(), (uint32_t)blocks.size() };
    int_t nodeId = newNode(solver, parents.size());
    VERIFY(nodeId == (int_t)block.blockId);

    blocks.push_back({ label, std::move(parents) });

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

    if (isNewLocation && hasActiveIndex(block.blockId)) {
        propagteActiveLink(solver, block, location, activeIndex(block.blockId), cache);
    }

    return cache.load;
}

void Phis::onIndexActivated(Solver& solver, int_t nodeId, int_t link) {
    BlockId block { (uint32_t)CodeBlockTheory::theoryId(), (uint32_t)nodeId };
    Phi& phi = get(block);
    for (int_t locIndex = 0; locIndex < phi.locations.size(); locIndex++) {
        propagteActiveLink(solver, block, phi.locations.keyAt(locIndex), link, phi.locations.at(locIndex));
    }
}

void Phis::propagteActiveLink(Solver& solver, BlockId block, MemoryLocation location, int_t link, CachedValues& cache) {
    std::optional<BooleanValue>& equality = cache.equalities[link];
    if (!equality.has_value()) {
        Value v = solver.loadAtEndOfBlock(location, get(block).parents[link]);
        equality = solver.equality(cache.load, v);
    }

    BooleanValue negatedCondition = indexInactiveLiteral(block.blockId, link);
    solver.assignTrue(equality.value(), implications.makeImplicationReason(negatedCondition, equality.value()));
}

Value Phis::loadAtPosition(Solver& solver, MemoryLocation location, CodePosition position) {
    return loadAtEndOfBlock(solver, location, position.block);
}

}