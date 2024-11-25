#pragma once

#include <check/CodeBlockTheory.h>
#include <check/ImplicationReasons.h>
#include <check/SimpleBooleanTheory.h>

#include <FlatTreeSet.h>

namespace check {

struct Solver;

struct OneOfTheory : SimpleBooleanTheory, private ReasonTheory {
    OneOfTheory(Solver&);

    BooleanValue indexActiveLiteral(int_t nodeId, int_t index) {
        return positiveLiteral(nodes[nodeId].firstVarId + index);
    }
    BooleanValue indexInactiveLiteral(int_t nodeId, int_t index) {
        return negativeLiteral(nodes[nodeId].firstVarId + index);
    }
    bool hasActiveIndex(int_t nodeId) {
        return nodes[nodeId].activeIndex != INVALID_ACTIVE_INDEX;
    }
    int_t activeIndex(int_t nodeId) {
        VERIFY(hasActiveIndex(nodeId));
        return nodes[nodeId].activeIndex;
    }

    int_t newNode(Solver&, int_t varCount);

    std::string formatPositiveLiteral(Solver&, int_t varId) override;
    std::string formatNegativeLiteral(Solver& solver, int_t varId) override;
    uint64_t labelOfValue(Solver&, Value) override;

    void propagateFalseAssignment(Solver&, BooleanValue) override;
    void reapplyFalseAssignment(Solver&, BooleanValue) override { }
    void unapplyFalseAssignment(Solver&, BooleanValue) override;

    int_t nodeCount() const { return nodes.size() - 1; }

    virtual void onIndexActivated(Solver&, [[maybe_unused]] int_t nodeId, [[maybe_unused]] int_t index) { }
    virtual std::string formatIndex(Solver&, int_t nodeId, int_t index) = 0;
    virtual uint64_t labelOfIndex(Solver&, int_t nodeId, int_t index, bool positive) = 0;

private:
    static constexpr uint32_t INVALID_ACTIVE_INDEX = -1;

    struct NodeInfo {
        uint32_t firstVarId;
        uint32_t activeIndex = INVALID_ACTIVE_INDEX;
    };

    int_t findNodeForVar(int_t linkVar);

    Reason makeOtherVarActiveReason(int_t nodeId, int_t activeVarId, int_t inactiveVarId);
    int_t reasonNodeId(const Reason&);
    int_t reasonActiveVarId(const Reason&);
    int_t reasonInactiveVarId(const Reason&);

    bool testReason(Solver&, const Reason&) override;
    ClauseAndIndex reasonToClause(Solver&, const Reason&) override;
    void newDecisionLevel(Solver&) override { }
    void backtrack(Solver&) override { }

    std::vector<NodeInfo> nodes;
};

template<typename T>
struct SolverSortedSet : FlatTreeSetDetail::Base<SolverSortedSet<T>, T> {
    using Base = FlatTreeSetDetail::Base<SolverSortedSet<T>, T>;

    uint32_t get(Solver& solver, const T& t) { return Base::get(solver, t); }

    std::strong_ordering compare(std::same_as<Solver> auto& solver, const T& a, const T& b) {
        return solver.compare(a, b);
    }

    uint32_t makeNode(Solver&, const T& t, TreeLabel label) {
        return Base::makeNode(label, t);
    }
};

template<typename K, typename T>
struct SolverSortedMap : FlatTreeSetDetail::Base<SolverSortedMap<K, T>, std::pair<K, T>> {
    using Base = FlatTreeSetDetail::Base<SolverSortedMap<K, T>, std::pair<K, T>>;

    uint32_t get(Solver& solver, const K& k) { return Base::get(solver, k); }

    std::strong_ordering compare(std::same_as<Solver> auto& solver, const K& a, const std::pair<K, T>& b) {
        return solver.compare(a, b.first);
    }

    uint32_t makeNode(Solver&, const K& k, TreeLabel label) {
        return Base::makeNode(label, std::make_pair(k, T()));
    }

    T& at(uint32_t handle) { return Base::at(handle).second; }
    K keyAt(uint32_t handle) { return Base::at(handle).first; }
};

struct Phis : CodeBlockTheory, private OneOfTheory {
    Phis(Solver&, uint64_t baseLabel);

    BlockId newPhi(Solver& solver, uint32_t label, std::vector<BlockId> parents);

    BooleanValue linkActiveLiteral(BlockId block, int_t linkIndex) {
        return indexActiveLiteral(block.blockId, linkIndex);
    }
    BooleanValue linkInactiveLiteral(BlockId block, int_t linkIndex) {
        return indexInactiveLiteral(block.blockId, linkIndex);
    }

    // CodeBlockTheory
    std::string formatBlockName(Solver&, BlockId) override;
    std::string formatCodePosition(Solver&, CodePosition) override;

    uint64_t labelOfBlock(Solver&, BlockId) override;

    Value loadAtEndOfBlock(Solver&, MemoryLocation, BlockId) override;
    Value loadAtPosition(Solver&, MemoryLocation, CodePosition) override;

private:
    struct CachedValues {
        Value load;
        std::optional<BooleanValue> equality;
    };

    struct LocationCache : FlatTreeSetDetail::Base<LocationCache, std::pair<MemoryLocation, CachedValues>> {

        uint32_t get(Solver& solver, MemoryLocation loc, BlockId block) {
            return Base::get(solver, loc, block);
        }

        std::strong_ordering compare(Solver& solver, MemoryLocation, BlockId, const std::pair<MemoryLocation, CachedValues>&);
        uint32_t makeNode(Solver&, MemoryLocation, BlockId, TreeLabel);

        MemoryLocation keyAt(uint32_t handle) { return Base::at(handle).first; }
        CachedValues& at(uint32_t handle) { return Base::at(handle).second; }
    };

    struct Phi {
        uint32_t label;
        std::vector<BlockId> parents;
        LocationCache locations = {};
    };

    std::string formatIndex(Solver&, int_t nodeId, int_t index) override;
    uint64_t labelOfIndex(Solver&, int_t nodeId, int_t index, bool positive) override;

    void onIndexActivated(Solver&, int_t nodeId, int_t index) override;
    void propagteActiveLink(Solver& solver, BlockId block, MemoryLocation location, int_t link, CachedValues& cache);

    Phi& get(BlockId block) { return blocks[block.blockId]; }

    std::vector<Phi> blocks;
    ImplicationReasons implications;
    uint64_t baseLabel;
};

}