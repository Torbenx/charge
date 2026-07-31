#include <verify/backend/InvariantPrefixes.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

void InvariantPrefixes::appendLetters(Solver& solver, InvariantWord word, std::vector<InvariantLetter>& out) {
    memberBuffer.clear();
    solver.impl().members.appendRewrite(word.member, memberBuffer);

    for (Member letter : memberBuffer) {
        // Stepping into a member steps below the location first, which puts the exclusive set of a
        // location above the sets of all of its members
        out.push_back(InvariantLetter::narrow());
        out.push_back(InvariantLetter::member(letter));
    }

    if (!word.suffix.isEmpty())
        out.push_back(word.suffix.toLetter());
}

void InvariantPrefixes::explainLetters(Solver& solver, InvariantWord word, ClauseBuilder& clause) {
    // Only the member of the location is rewritten
    solver.impl().members.explainRewrite(solver, word.member, clause);
}

}
