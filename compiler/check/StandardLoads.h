#pragma once

#include <check/LoadSet.h>
#include <check/SatSolver.h>
#include <check/EqualityInfo.h>

namespace check {

struct StandardLoads : EquatableValueTheory, private LoadSet<StandardLoads, EquatableValueTheory::EqualityInfo> {
    using EquatableValueTheory::EquatableValueTheory;

    Value defineLoad(Solver& solver, MemoryLocation loc, CodePosition pos) {
        auto id = LoadSet::get(solver, loc, pos);
        return Value { (uint32_t)theoryId(), id };
    }

    void collectValueInactiveReasons(Solver& solver, Value v, std::vector<BooleanValue>& clause) override {
        LoadSet::collectLoadInactiveReasons(solver, v.valueId, clause);
    }

    bool isValueActive(Solver& solver, Value v) override {
        return LoadSet::isLoadActive(solver, v.valueId);
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        return baseLabel + (uint64_t)LoadSet::label(v.valueId);
    }

    EqualityInfo& equalityInfo(Solver&, Value v) override {
        return LoadSet::at(v.valueId);
    }

    std::string formatValue(Solver& solver, Value v) override {
        auto [loc, pos] = LoadSet::loadAt(v.valueId);
        return solver.formatLoad(loc, pos);
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = 0; i < LoadSet::size(); i++)
            f(Value { (uint32_t)theoryId(), (uint32_t)i });
    }

private:
    uint64_t baseLabel = 0;

    EqualityInfo makeData(Solver&, uint32_t newId, MemoryLocation, CodePosition) {
        return EqualityInfo({ (uint32_t)theoryId(), newId });
    }

    friend LoadSet;
};

}