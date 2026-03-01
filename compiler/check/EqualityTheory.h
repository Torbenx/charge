#pragma once

#include <check/SimpleBooleanTheory.h>

#include <FlatTreeSet.h>

namespace check {

struct EqualityTheory : SimpleBooleanTheory {
    using Link = OrientedPair;

    using SimpleBooleanTheory::SimpleBooleanTheory;

    std::string formatPositiveLiteral(Solver&, int_t varId) override;
    std::string formatNegativeLiteral(Solver&, int_t varId) override;

    void collectVariableInactiveReasons(Solver&, int_t, std::vector<BooleanValue>&) override;
    bool isVariableActive(Solver&, int_t) override;

protected:
    //! Must return the link for \p eqId
    virtual Link equalityLink(int_t eqId) = 0;
};

}