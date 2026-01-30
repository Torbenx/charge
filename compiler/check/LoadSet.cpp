#include <check/LoadSet.h>
#include <check/SatSolver.h>

namespace check::LoadSetDetail {

void collectLoadInactiveReasons(Solver& solver, const Load& load, std::vector<BooleanValue>& clause) {
    solver.collectInactiveReasons(load.location, clause);
    clause.push_back(solver.negate(solver.blockActiveLiteral(load.position.block)));
}

bool isLoadActive(Solver& solver, const Load& load) {
    return solver.isActive(load.location) && solver.assignedTrue(solver.blockActiveLiteral(load.position.block));
}

}