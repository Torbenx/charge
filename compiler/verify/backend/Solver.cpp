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
    case ReasonKind::Clause:
        return impl.clauses.testReason(impl, lit, reason);
    default:
        VERIFY_NOT_REACHED();
    }
}

ClauseAndIndex SatCore::Interface::reasonToClause(Literal lit, const Reason& reason) {
    auto& impl = static_cast<SolverImpl&>(*this);
    switch (reason.kind()) {
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

void SatCore::Interface::learnClause(std::span<const Literal> clause) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.clauses.addClause(impl, { clause.begin(), clause.end() });
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