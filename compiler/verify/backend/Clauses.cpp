#include <verify/backend/Clauses.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

Clauses::Clauses(Solver& solver)
    : instances(solver, ValueKind::Boolean) { }

bool Clauses::testReason(Solver&, BooleanValue, const Reason& reason) {
    int_t clauseIndex = reason.get<ReasonKind::Clause>().clauseIndex;
    return std::popcount(clauseMasks[clauseIndex]) == 1;
}

ClauseAndIndex Clauses::reasonToClause(Solver&, BooleanValue, const Reason& reason) {
    auto data = reason.get<ReasonKind::Clause>();
    return { clauses[data.clauseIndex], data.literalIndex };
}

void Clauses::propagateAssignment(Solver& solver, BooleanValue literal) {
    // VERIFY(info.assignedFalse());
    // VERIFY(!literalTheory.getInfo(literalTheory.negate(literal)).assignedFalse());

    for (auto inst : instances[!literal]) {
        clause_mask_t& clauseMask = clauseMasks[inst.clauseIndex];
        // Perform the popcount before we clear the bit so the operations can be executed in parallel
        int popcnt = std::popcount(clauseMask);

        // solver.dumpClause(solver.clauses[inst.clauseIndex]);
        // println("{:#032b} - {:#032b} = {:#032b}", clauseMask, literalMask(inst.literalIndex), clauseMask & ~literalMask(inst.literalIndex));

        // VERIFY((clauseMask & literalMask(inst.literalIndex)) != (clause_mask_t)0);
        clauseMask &= ~literalMask(inst.literalIndex);

        // Detect if the clause has only one non-false literal (only one bit set).
        // Since popcnt still counts the bit we just cleared we must test against 2 instead of 1.
        if (popcnt > 2)
            continue;

        VERIFY(popcnt == 2);

        // Unit clause propagation:
        // All other literals in this clause are false thus the last one must be true.
        const auto& clause = clauses[inst.clauseIndex];

        int_t trueLitIndex = std::countr_zero(clauseMask);
        BooleanValue trueLit = clause[trueLitIndex];
        LiteralInstance trueLitInst = inst;
        trueLitInst.literalIndex = trueLitIndex;
        solver.assignTrue(trueLit, makeReason<ReasonKind::Clause>(trueLitInst));
    }
}

void Clauses::unapplyAssignment(Solver& solver, BooleanValue literal) {
    for (auto inst : instances[!literal]) {
        auto& clauseMask = clauseMasks[inst.clauseIndex];
        auto mask = literalMask(inst.literalIndex);
        clauseMask |= mask;
    }
}

void Clauses::addClause(Solver& solver, std::span<const BooleanValue> clause) {
    /* TODO:
    if (simplifyClause(clause))
        return;

    if (clause.size() == 1) {
        unitAssignTrue(clause[0]);
        return;
    }*/

    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE * (MAX_CLAUSE_SIZE - 1));
    if ((int_t)clause.size() <= MAX_CLAUSE_SIZE) {
        addClauseInternal(solver, { clause.begin(), clause.end() });
        return;
    }
    // clause.size <= (MAX_CLAUSE_SIZE - extraClauses) + extraClauses * (MAX_CLAUSE_SIZE - 1)
    // -> extraClauses >= (clause.size - MAX_CLAUSE_SIZE) / (MAX_CLAUSE_SIZE - 2)
    // -> extraClauses >= floor( (clause.size - MAX_CLAUSE_SIZE + MAX_CLAUSE_SIZE - 3) / (MAX_CLAUSE_SIZE - 2)
    int_t extraClauses = ((int_t)clause.size() - 3) / (MAX_CLAUSE_SIZE - 2);

    // println("packing {} literals into {} clauses", clause.size(), extraClauses + 1);
    // print("clause: "); dumpClause(clause);

    int_t takenCount = 0;
    auto take = [&](std::vector<BooleanValue>& into, int_t n) {
        VERIFY((int_t)clause.size() > takenCount);
        n = std::min(n, (int_t)clause.size() - takenCount);
        for (int_t i = 0; i < n; i++)
            into.push_back(clause[takenCount++]);
    };

    std::vector<BooleanValue> primaryClause;
    primaryClause.reserve(MAX_CLAUSE_SIZE);
    take(primaryClause, MAX_CLAUSE_SIZE - extraClauses);

    for (int_t i = 0; i < extraClauses; i++) {
        std::vector<BooleanValue> extraClause;
        extraClause.reserve(MAX_CLAUSE_SIZE);
        BooleanValue glueLit = solver.impl().newBoolean(TheoryId::ClauseGlueVariables);
        primaryClause.push_back(glueLit);
        extraClause.push_back(!glueLit);
        take(extraClause, MAX_CLAUSE_SIZE - 1);
        VERIFY(extraClause.size() >= 3);
        // print("extra: "); dumpClause(extraClause);
        addClauseInternal(solver, std::move(extraClause));
    }

    VERIFY(primaryClause.size() == MAX_CLAUSE_SIZE);
    // print("primary: "); dumpClause(primaryClause);
    addClauseInternal(solver, std::move(primaryClause));

    VERIFY(takenCount == (int_t)clause.size());
}

void Clauses::addClauseInternal(Solver& solver, std::vector<BooleanValue> clause) {
    VERIFY(!clause.empty());
    VERIFY((int_t)clause.size() <= MAX_CLAUSE_SIZE);
    VERIFY(clauses.size() == clauseMasks.size());
    int_t clauseIndex = clauses.size();
    clause_mask_t mask = 0;
    for (int_t index = 0; index < (int_t)clause.size(); index++) {
        LiteralInstance inst { (uint32_t)index, (uint32_t)clauseIndex };
        BooleanValue lit = clause[index];
        instances[lit].push_back(inst);
        if (!solver.assignedFalse(lit))
            mask |= literalMask(index);
    }
    VERIFY(mask != 0);
    clauses.emplace_back(std::move(clause));
    clauseMasks.push_back(mask);
    if (std::popcount(mask) == 1) {
        int_t index = std::countr_zero(mask);
        LiteralInstance inst { .literalIndex = (uint32_t)index, .clauseIndex = (uint32_t)clauseIndex };
        solver.assignTrue(clauses.back()[index], makeReason<ReasonKind::Clause>(inst));
    }
}

bool Clauses::checkAssignment(Solver& solver) {
    for (const auto& clause : clauses) {
        bool foundTrue = false;
        std::optional<BooleanValue> unassignedGlueLiteral;
        for (BooleanValue lit : clause) {
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