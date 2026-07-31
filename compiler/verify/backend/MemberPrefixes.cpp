#include <verify/backend/MemberPrefixes.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

void MemberPrefixes::appendLetters(Solver& solver, Member expression, std::vector<Member>& out) {
    solver.impl().members.appendRewrite(expression, out);
}

void MemberPrefixes::explainLetters(Solver& solver, Member expression, ClauseBuilder& clause) {
    solver.impl().members.explainRewrite(solver, expression, clause);
}

}
