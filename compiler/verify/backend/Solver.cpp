#include <verify/backend/SolverImpl.h>

namespace verify::backend {

// --------------------------- Constructor --------------------------

std::unique_ptr<Solver> Solver::make() {
    return std::make_unique<SolverImpl>();
}

Solver::Solver() { }

SolverImpl::SolverImpl()
    : literalInfos(*this, ValueKind::Boolean)
    , clauses(*this)
    , auxBoolNames(*this, TheoryId::AuxBooleanVariables) { }

// ------------------------ SatCore forwards ------------------------

int_t Solver::currentDecisionLevel() const {
    return impl().sat.currentDecisionLevel();
}
bool Solver::assignedTrue(BooleanValue lit) {
    return impl().sat.assignedTrue(lit);
}
bool Solver::assignedFalse(BooleanValue lit) {
    return impl().sat.assignedFalse(lit);
}
void Solver::decideTrue(BooleanValue literal) {
    impl().sat.decideTrue(literal);
}
void Solver::assignTrue(BooleanValue trueLit, const Reason& reason) {
    impl().sat.assignTrue(trueLit, reason);
}
bool Solver::alwaysTrue(BooleanValue value) {
    return impl().sat.alwaysTrue(value);
}

// ------------------------ SatCore callbacks -----------------------

void SatCore::Interface::onNewDecisionLevel() {
    auto& impl = static_cast<SolverImpl&>(*this);
}

void SatCore::Interface::onBacktrack() {
    auto& impl = static_cast<SolverImpl&>(*this);
}

bool SatCore::Interface::testReason(Literal lit, const Reason& reason) {
    auto& impl = static_cast<SolverImpl&>(*this);
    switch (reason.kind()) {
    case ReasonKind::Always:
        return true;
    case ReasonKind::Clause:
        return impl.clauses.testReason(impl, lit, reason);
    default:
        VERIFY_NOT_REACHED();
    }
}

ClauseAndIndex SatCore::Interface::reasonToClause(Literal lit, const Reason& reason) {
    auto& impl = static_cast<SolverImpl&>(*this);
    switch (reason.kind()) {
    case ReasonKind::Always: {
        auto& clause = impl.scratchClause();
        clause.push_back(lit);
        return { clause, 0 };
    }
    case ReasonKind::Clause:
        return impl.clauses.reasonToClause(impl, lit, reason);
    default:
        VERIFY_NOT_REACHED();
    }
}

void SatCore::Interface::propagateAssignment(Literal lit) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.clauses.propagateAssignment(impl, lit);
}

void SatCore::Interface::unapplyAssignment(Literal lit) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.clauses.unapplyAssignment(impl, lit);
}

void SatCore::Interface::reapplyAssignment(Literal) {
    auto& impl = static_cast<SolverImpl&>(*this);
}

void SatCore::Interface::learnClause(std::vector<BooleanValue> clause) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.addClause(std::move(clause));
}

// ------------------------ Clauses forwards ------------------------

static bool simplifyClause(Solver& solver, std::vector<BooleanValue>& clause) {
    bool alwaysTrue = false;
    auto newEnd = std::partition(clause.begin(), clause.end(), [&](BooleanValue lit) {
        if (solver.alwaysTrue(lit))
            alwaysTrue = true;
        return !solver.alwaysFalse(lit);
    });
    if (alwaysTrue)
        return true;

    // The clause can end up empty when all literals are false due to unit clauses.
    // In that case the problem is always unsatisfiable, but the clause must kept to determine that.
    if (newEnd != clause.begin())
        clause.erase(newEnd, clause.end());

    return false;
}

void Solver::addClause(std::vector<BooleanValue> clause) {
    if (simplifyClause(*this, clause))
        return;

    if (clause.size() == 1) {
        assignTrue(clause[0], makeReason<ReasonKind::Always>({}));
        return;
    }

    impl().clauses.addClause(*this, clause);
}

// -------------------------- Data forwards -------------------------

int_t Solver::valueCount(TheoryId theory) {
    return impl().data.at(theory).valueCount;
}

int_t Solver::booleanCount(TheoryId theory) {
    VERIFY(kindOf(theory) == ValueKind::Boolean);
    return valueCount(theory) / 2;
}

// ------------------------- Value factories ------------------------

Value SolverImpl::newValue(TheoryId theory) {
    return data.newValue(theory);
}

BooleanValue SolverImpl::newBoolean(TheoryId theory) {
    VERIFY(kindOf(theory) == ValueKind::Boolean);
    BooleanValue result(newValue(theory));
    newValue(theory);
    return result;
}

BooleanValue Solver::newAuxBoolean(std::string name) {
    BooleanValue v = impl().newBoolean(TheoryId::AuxBooleanVariables);
    impl().auxBoolNames[v] = std::move(name);
    return v;
}

}