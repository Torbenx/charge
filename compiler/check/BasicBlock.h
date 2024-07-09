#pragma once

#include <types.h>
#include <check/Value.h>

#include <list>

namespace check {

struct BasicBlock {
    enum class Kind : uint8_t {
        Store,
        Phi,
    };

    Kind kind;
    BasicBlock* activeBlock = nullptr;
};

struct Phi : BasicBlock {
    struct Parent {
        BasicBlock* block;
        BooleanValue dominanceFrontierCondition;
    };

    int_t activeIndex;
    std::vector<Parent> parents;
};

struct StoreBlock : BasicBlock {
    struct Store {
        MemoryLocation location;
        Value value;
    };

    StoreBlock(std::optional<BasicBlock*> parent)
        : parent(parent) { }

    std::optional<BasicBlock*> parent;
    std::vector<Store> stores;
};

//! Represent an execution position immediately after the given instruction
struct CodeIterator {
    BasicBlock* block = nullptr;
    int_t position;
};

struct Loads : EquatableValueTheory {
    struct LoadInfo {
        MemoryLocation location;
        CodeIterator position;
        EqualityInfo equalityInfo;
    };

    Loads(Solver& solver)
        : EquatableValueTheory(solver) { }

    Value load(MemoryLocation loc, CodeIterator pos) {

    }

    Type typeOf(Solver& solver, Value v) override {
        MemoryLocation loc = infoFor(v).location;
        return solver.theoryFor(loc).typeAtLocation(solver, loc);
    }

    uint64_t labelOf(Solver&, Value v) override {
        return baseLabel + (uint64_t)loads.label(v.valueId);
    }

    EqualityInfo& equalityInfo(Solver&, Value v) override {
        return infoFor(v).equalityInfo;
    }

    void propagateEquality(Solver&, Value, Value) override { }

    std::string formatValue(Solver& solver, Value v) override {
        std::string result = "load(";
        result += solver.formatValue(infoFor(v).location);
        result += " @ ";
        // TODO: Format position
        result += ")";
        return result;
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = 0; i < loads.nodeCount(); i++)
            f(Value { (uint32_t)theoryId(), (uint32_t)i });
    }

private:
    LoadInfo& infoFor(Value v) { return loads.at(v.valueId); }

    FlatTreeSet<LoadInfo> loads;
    uint64_t baseLabel = 0;
};

struct DominanceConditions : SimpleBooleanTheory<> {

};

struct BlockManager {
    using Callback = std::function<void()>;

    BlockManager(Solver& solver)
        : solver(solver) { }

    void store(MemoryLocation location, Value value) {
        if (!currentStoreBlock.has_value())
            currentStoreBlock = new StoreBlock(currentParentBlock);
        currentStoreBlock->stores.push_back({ location, value });
    }

    void dynamicTerminate() {
        currentPositionReachable = false;
    }

    void proveUnreachable() {
        currentPositionReachable = false;
    }

    void branch(BooleanValue condition, Callback ifTrue, Callback ifFalse) {
        VERIFY(currentPositionReachable);
        commitStores();
        BasicBlock* parentOfBranch = currentParentBlock;

        ifTrue();
        commitStores();
        BasicBlock* trueParent = currentParentBlock;
        bool trueEndReachable = currentPositionReachable;

        currentParentBlock = parentOfBranch;
        ifFalse();
        commitStores();
        BasicBlock* falseParent = currentParentBlock;
        bool falseEndReachable = currentPositionReachable;

        Phi* phi = new Phi();
        if (trueEndReachable)
            phi->parents.push_back({ trueParent, condition });
        if (falseEndReachable)
            phi->parents.push_back({ falseParent, solver.negate(condition) });
        currentPositionReachable = trueEndReachable || falseEndReachable;
        currentParentBlock = phi;
    }

    void branch(BooleanValue condition, Callback ifTrue) {
        branch(condition, std::move(ifTrue), [] {});
    }

private:
    void commitStores() {
        if (currentStoreBlock.has_value()) {
            currentParentBlock = currentStoreBlock;
            currentStoreBlock = nullptr;
        }
    }

    bool currentPositionReachable = true;
    std::optional<BasicBlock*> currentParentBlock; //!< Block that should the parent of the next block
    std::optional<StoreBlock*> currentStoreBlock; //!< The block currently being build

    Solver& solver;
};

}