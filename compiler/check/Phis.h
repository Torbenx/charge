#pragma once

#include <check/BooleanVariables.h>
#include <check/CodeBlockTheory.h>
#include <check/Enums.h>
#include <check/Reason.h>


#include <FlatTreeSet.h>

namespace check {

struct Solver;

struct Phis : CodeBlockTheory {
    Phis(Solver&, uint64_t baseLabel);

    BlockId newPhi(Solver& solver, uint32_t label, std::vector<BlockId> parents);

    BooleanValue linkActiveLiteral(Solver& solver, BlockId block, int_t linkIndex) {
        return links.elementActiveLiteral(solver, block.blockId, linkIndex);
    }
    BooleanValue linkInactiveLiteral(Solver& solver, BlockId block, int_t linkIndex) {
        return links.elementInactiveLiteral(solver, block.blockId, linkIndex);
    }
    bool hasActiveLink(BlockId block) { return links.hasActiveElement(block.blockId); }
    int_t activeLink(BlockId block) { return links.activeElement(block.blockId); }

    // CodeBlockTheory
    std::string formatBlockName(Solver&, BlockId) override;
    std::string formatCodePosition(Solver&, CodePosition) override;

    uint64_t labelOfBlock(Solver&, BlockId) override;

    Value loadAtEndOfBlock(Solver&, MemoryLocation, BlockId) override;
    Value loadAtPosition(Solver&, MemoryLocation, CodePosition) override;

    BooleanValue blockActiveLiteral(Solver&, BlockId) override;

private:
    struct CachedValues {
        Value load;
        std::unique_ptr<std::optional<BooleanValue>[]> equalities;
    };

    struct Links : SetElements {
        Links(Solver& solver, uint64_t baseLabel)
            : SetElements(solver), baseLabel(baseLabel) { }
        Phis* phis();
        std::string formatElement(Solver&, int_t setId, int_t index) override;
        uint64_t labelOfElement(Solver&, int_t setId, int_t index, bool positive) override;
        void onElementActivated(Solver&, int_t setId, int_t link) override;

        uint64_t baseLabel;
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

    struct Phi {
        uint32_t label;
        std::vector<BlockId> parents;
        LocationCache locations = {};
    };

    void propagteActiveLink(Solver& solver, BlockId block, MemoryLocation location, int_t link, CachedValues& cache);

    Phi& get(BlockId block) { return blocks[block.blockId]; }
    BlockId blockFromId(int_t id) { return { (uint32_t)CodeBlockTheory::theoryId(), (uint32_t)id }; }

    std::vector<Phi> blocks;
    Links links;
};

}