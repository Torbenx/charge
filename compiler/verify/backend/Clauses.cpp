#include <verify/backend/Clauses.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

Clauses::Clauses(Solver& solver)
    : occMap(solver, Sort::Boolean) { }

bool Clauses::testReason(Solver&, Bool, const Reason& reason) {
    int_t clauseIndex = reason.get<ReasonKind::Clause>().clauseIndex;
    return std::popcount(clauseMasks[clauseIndex]) == 1;
}

ClauseAndIndex Clauses::reasonToClause(Solver&, Bool, const Reason& reason) {
    auto data = reason.get<ReasonKind::Clause>();
    return { clauses[data.clauseIndex], data.literalIndex };
}

void Clauses::propagateAssignment(Solver& solver, Bool literal) {
    // VERIFY(info.assignedFalse());
    // VERIFY(!literalTheory.getInfo(literalTheory.negate(literal)).assignedFalse());

    for (auto occ : occMap[!literal]) {
        clause_mask_t& clauseMask = clauseMasks[occ.clauseIndex];
        // Perform the popcount before we clear the bit so the operations can be executed in parallel
        int popcnt = std::popcount(clauseMask);

        // solver.dumpClause(solver.clauses[occ.clauseIndex]);
        // dbgln("{:#032b} - {:#032b} = {:#032b}", clauseMask, literalMask(occ.literalIndex), clauseMask & ~literalMask(occ.literalIndex));

        // VERIFY((clauseMask & literalMask(occ.literalIndex)) != (clause_mask_t)0);
        clauseMask &= ~literalMask(occ.literalIndex);

        // Detect if the clause has only one non-false literal (only one bit set).
        // Since popcnt still counts the bit we just cleared we must test against 2 instead of 1.
        if (popcnt > 2)
            continue;

        VERIFY(popcnt == 2);

        // Unit clause propagation:
        // All other literals in this clause are false thus the last one must be true.
        const auto& clause = clauses[occ.clauseIndex];

        int_t trueLitIndex = std::countr_zero(clauseMask);
        LiteralOccurrence trueLitOcc = occ;
        trueLitOcc.literalIndex = trueLitIndex;
        solver.assignTrue(clause[trueLitIndex], makeReason<ReasonKind::Clause>(trueLitOcc));
    }
}

void Clauses::unapplyAssignment(Solver&, Bool literal) {
    for (auto occ : occMap[!literal]) {
        auto& clauseMask = clauseMasks[occ.clauseIndex];
        auto mask = literalMask(occ.literalIndex);
        clauseMask |= mask;
    }
}

void Clauses::addClause(Solver& solver, std::vector<Bool> clause) {
    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE * (MAX_CLAUSE_SIZE - 1));
    if ((int_t)clause.size() <= MAX_CLAUSE_SIZE) {
        addClauseInternal(solver, std::move(clause));
        return;
    }
    // clause.size <= (MAX_CLAUSE_SIZE - extraClauses) + extraClauses * (MAX_CLAUSE_SIZE - 1)
    // -> extraClauses >= (clause.size - MAX_CLAUSE_SIZE) / (MAX_CLAUSE_SIZE - 2)
    // -> extraClauses >= floor( (clause.size - MAX_CLAUSE_SIZE + MAX_CLAUSE_SIZE - 3) / (MAX_CLAUSE_SIZE - 2)
    int_t extraClauses = ((int_t)clause.size() - 3) / (MAX_CLAUSE_SIZE - 2);

    // dbgln("packing {} literals into {} clauses", clause.size(), extraClauses + 1);
    // dbgprint("clause: "); dumpClause(clause);

    int_t takenCount = 0;
    auto take = [&](std::vector<Bool>& into, int_t n) {
        VERIFY((int_t)clause.size() > takenCount);
        n = std::min(n, (int_t)clause.size() - takenCount);
        for (int_t i = 0; i < n; i++)
            into.push_back(clause[takenCount++]);
    };

    std::vector<Bool> primaryClause;
    primaryClause.reserve(MAX_CLAUSE_SIZE);
    take(primaryClause, MAX_CLAUSE_SIZE - extraClauses);

    for (int_t i = 0; i < extraClauses; i++) {
        std::vector<Bool> extraClause;
        extraClause.reserve(MAX_CLAUSE_SIZE);
        Bool glueLit = solver.newBoolean(TheoryId::ClauseGlueVariables);
        primaryClause.push_back(glueLit);
        extraClause.push_back(!glueLit);
        take(extraClause, MAX_CLAUSE_SIZE - 1);
        VERIFY(extraClause.size() >= 3);
        // dbgprint("extra: "); dumpClause(extraClause);
        addClauseInternal(solver, std::move(extraClause));
    }

    VERIFY(primaryClause.size() == MAX_CLAUSE_SIZE);
    // dbgprint("primary: "); dumpClause(primaryClause);
    addClauseInternal(solver, std::move(primaryClause));

    VERIFY(takenCount == (int_t)clause.size());
}

void Clauses::addClauseInternal(Solver& solver, std::vector<Bool> clause) {
    VERIFY(!clause.empty());
    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE);
    VERIFY(clauses.size() == clauseMasks.size());
    int_t clauseIndex = clauses.size();
    clause_mask_t mask = 0;
    for (int_t index = 0; index < (int_t)clause.size(); index++) {
        LiteralOccurrence occ { (uint32_t)index, (uint32_t)clauseIndex };
        Bool lit = clause[index];
        occMap[lit].push_back(occ);
        if (!solver.assignedFalse(lit))
            mask |= literalMask(index);
    }
    if (mask == 0) {
        // This breaks our invariances of having always at least one bit set.
        // We can rely on this because the conflicts themselves are never propagated.
        // To recover the invariance we glue the conflict clause to a an empty one,
        // the glue variable will be initially unassigned have its bit set.
        // TODO: What when clause.size() == MAX_CLAUSE_SIZE
        VERIFY(clause.size() < MAX_CLAUSE_SIZE);
        Bool glue = solver.newBoolean(TheoryId::ClauseGlueVariables);
        occMap[glue].push_back({ (uint32_t)clause.size(), (uint32_t)clauseIndex });
        mask |= literalMask(clause.size());
        clause.push_back(glue);
        solver.assignTrue(!glue, makeReason<ReasonKind::Always>({}));
    }
    clauses.emplace_back(std::move(clause));
    clauseMasks.push_back(mask);
    if (std::popcount(mask) == 1) {
        int_t index = std::countr_zero(mask);
        LiteralOccurrence occ { .literalIndex = (uint32_t)index, .clauseIndex = (uint32_t)clauseIndex };
        solver.assignTrue(clauses.back()[index], makeReason<ReasonKind::Clause>(occ));
    }
}

bool Clauses::checkAssignment(Solver& solver) {
    for (const auto& clause : clauses) {
        bool foundTrue = false;
        std::optional<Bool> unassignedGlueLiteral;
        for (Bool lit : clause) {
            if (solver.assignedTrue(lit)) {
                foundTrue = true;
                break;
            }
            if (lit.theory() == TheoryId::ClauseGlueVariables && !solver.assignedFalse(lit))
                unassignedGlueLiteral = lit;
        }
        if (!foundTrue) {
            if (!unassignedGlueLiteral.has_value())
                return false;
            solver.decideTrue(unassignedGlueLiteral.value());
            if (!solver.impl().sat.propagate())
                return false;
        }
    }
    return true;
}

}