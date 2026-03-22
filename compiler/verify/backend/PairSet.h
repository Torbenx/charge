#pragma once

#include <verify/backend/Solver.h>

#include <FlatTreeSet.h>

namespace verify::backend {

struct PairSet : FlatTreeSetDetail::Base<PairSet, Pair> {
    uint32_t get(Solver&, Pair);
private:
    friend Base;
    uint32_t makeNode(Solver&, Pair, TreeLabel);
    std::strong_ordering compare(Solver&, Pair, Pair);
};

}