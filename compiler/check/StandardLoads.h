#pragma once

#include <check/LoadSet.h>
#include <check/SatSolver.h>

namespace check {

struct StandardLoads : EquatableValueTheory, private LoadSet<StandardLoads, EquatableValueTheory::EqualityInfo> {
    using EquatableValueTheory::EquatableValueTheory;

    Value load(Solver& solver, MemoryLocation loc, CodePosition pos) {
        auto id = LoadSet::get(solver, loc, pos);
        return Value { (uint32_t)theoryId(), id };
    }

    uint64_t labelOfValue(Solver&, Value v) override {
        return baseLabel + (uint64_t)LoadSet::label(v.valueId);
    }

    EqualityInfo& equalityInfo(Solver&, Value v) override {
        return LoadSet::at(v.valueId);
    }

    std::string formatValue(Solver& solver, Value v) override {
        auto [loc, pos] = LoadSet::loadAt(v.valueId);
        std::string result = "load(";
        result += solver.formatValue(loc);
        result += " @ ";
        // TODO: Format position
        result += ")";
        return result;
    }

    void enumerateValues(Solver&, std::function<void(Value)> f) override {
        for (int_t i = 0; i < LoadSet::size(); i++)
            f(Value { (uint32_t)theoryId(), (uint32_t)i });
    }

private:
    uint64_t baseLabel = 0;

    EqualityInfo makeData(uint32_t newId) {
        return EqualityInfo({ (uint32_t)theoryId(), newId });
    }

    friend LoadSet;
};

}