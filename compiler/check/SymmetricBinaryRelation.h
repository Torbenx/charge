#pragma once

#include <FlatTreeSet.h>
#include <check/Value.h>

namespace check {

template<typename D>
struct OrientedPairSet : FlatTreeSetDetail::Base<OrientedPairSet<D>, D> {
    using Base = FlatTreeSetDetail::Base<OrientedPairSet<D>, D>;
    uint32_t get(Solver& solver, const OrientedPair& pair) {
        return Base::get(solver, pair);
    }

private:
    friend Base;
    uint32_t makeNode(Solver&, const OrientedPair& pair, TreeLabel label) {
        return Base::makeNode(label, D(pair));
    }
    std::strong_ordering compare(std::same_as<Solver> auto& solver, const OrientedPair& a, const D& d) {
        OrientedPair b = (OrientedPair)d;
        auto targetCmp = solver.compare(a.target, b.target);
        if (targetCmp != 0)
            return targetCmp;
        return solver.compare(a.source, b.source);
    }
};

template<typename D = OrientedPair>
struct SymmetricBinaryRelation : OrientedPairSet<D> {
    int_t get(Solver& solver, Value a, Value b) {
        auto pair = OrientedPair::orient(solver, a, b);
        return OrientedPairSet<D>::get(solver, pair);
    }
    int_t get(Solver& solver, OrientedPair pair) {
        return OrientedPairSet<D>::get(solver, pair);
    }
};

}