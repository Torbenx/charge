#pragma once

#include <check/BooleanVariables.h>
#include <check/CodeBlockTheory.h>
#include <check/ImplicationReasons.h>

#include <FlatTreeSet.h>

namespace check {

struct Solver;

struct Phis : CodeBlockTheory {
    Phis(Solver&, uint64_t baseLabel);

    BlockId newPhi(Solver& solver, uint32_t label, std::vector<BlockId> parents);

    BooleanValue linkActiveLiteral(BlockId block, int_t linkIndex) {
        return ones.indexActiveLiteral(block.blockId, linkIndex);
    }
    BooleanValue linkInactiveLiteral(BlockId block, int_t linkIndex) {
        return ones.indexInactiveLiteral(block.blockId, linkIndex);
    }
    bool hasActiveLink(BlockId block) { return ones.hasActiveIndex(block.blockId); }
    int_t activeLink(BlockId block) { return ones.activeIndex(block.blockId); }

    // CodeBlockTheory
    std::string formatBlockName(Solver&, BlockId) override;
    std::string formatCodePosition(Solver&, CodePosition) override;

    uint64_t labelOfBlock(Solver&, BlockId) override;

    Value loadAtEndOfBlock(Solver&, MemoryLocation, BlockId) override;
    Value loadAtPosition(Solver&, MemoryLocation, CodePosition) override;

    BooleanValue blockActiveLiteral(Solver&, BlockId) override;

private:
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

        int_t newNode(Solver&, int_t varCount, BooleanValue activityPrecondition);

        std::string formatPositiveLiteral(Solver&, int_t varId) override;
        std::string formatNegativeLiteral(Solver& solver, int_t varId) override;
        uint64_t labelOfValue(Solver&, Value) override;

        void propagateFalseAssignment(Solver&, BooleanValue) override;
        void reapplyFalseAssignment(Solver&, BooleanValue) override { }
        void unapplyFalseAssignment(Solver&, BooleanValue) override;

        int_t nodeCount() const { return nodes.size() - 1; }

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

        Phis* phis();
    };

    struct CachedValues {
        Value load;
        std::unique_ptr<std::optional<BooleanValue>[]> equalities;
    };

    struct LocationCache : FlatTreeSetDetail::Base<LocationCache, std::pair<MemoryLocation, CachedValues>> {

        uint32_t get(Solver& solver, MemoryLocation loc, BlockId block, int_t parentCount) {
            return Base::get(solver, loc, block, parentCount);
        }

        std::strong_ordering compare(Solver& solver, MemoryLocation, BlockId, int_t, const std::pair<MemoryLocation, CachedValues>&);
        uint32_t makeNode(Solver&, MemoryLocation, BlockId, int_t, TreeLabel);

        MemoryLocation keyAt(uint32_t handle) { return Base::at(handle).first; }
        CachedValues& at(uint32_t handle) { return Base::at(handle).second; }
    };

    struct BlockActiveVariables : BooleanVariables {
        using BooleanVariables::BooleanVariables;
        Phis* phis();
        std::string formatPositiveLiteral(Solver&, int_t varId) override;
        std::string formatNegativeLiteral(Solver&, int_t varId) override;
    };

    struct Phi {
        uint32_t label;
        std::vector<BlockId> parents;
        LocationCache locations = {};
    };

    std::string formatIndex(Solver&, int_t nodeId, int_t index);
    uint64_t labelOfIndex(Solver&, int_t nodeId, int_t index, bool positive);

    void onIndexActivated(Solver&, int_t nodeId, int_t link);
    void propagteActiveLink(Solver& solver, BlockId block, MemoryLocation location, int_t link, CachedValues& cache);

    Phi& get(BlockId block) { return blocks[block.blockId]; }
    BlockId blockFromId(int_t id) { return { (uint32_t)CodeBlockTheory::theoryId(), (uint32_t)id }; }

    std::vector<Phi> blocks;
    OneOfTheory ones;
    ImplicationReasons implications;
    BlockActiveVariables blockActiveVariables;
    uint64_t baseLabel;
};

}