#pragma once

#include <check/LoadSet.h>
#include <check/SatSolver.h>

namespace check {

struct StandardLoads : ValueTheory, private LoadSet<StandardLoads, void> {
    StandardLoads(Solver& solver, ValueKind valuesKind)
        : ValueTheory(solver, valuesKind), baseLabel(solver, ValueCategory::Load) { }

    Value defineLoad(Solver& solver, MemoryLocation loc, CodePosition pos) {
        auto id = LoadSet::get(solver, loc, pos);
        return Value { (uint32_t)theoryId(), id };
    }

    std::optional<Load> loadInfo(Solver&, Value v) override {
        return LoadSet::loadAt(v.valueId);
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

    std::string formatValue(Solver& solver, Value v) override {
        auto [loc, pos] = LoadSet::loadAt(v.valueId);
        return solver.formatLoad(loc, pos);
    }

private:
    ValueBaseLabel baseLabel;

    void makeData(Solver&, uint32_t, MemoryLocation, CodePosition) { }

    friend LoadSet;
};

}