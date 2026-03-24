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
    , builtinTrueFalse(*this)
    , rewriteEqualities(*this)
    , auxBoolNames(*this, TheoryId::AuxBooleanVariables) { }

SolverImpl::BuiltinTrueFalse::BuiltinTrueFalse(Solver& solver) {
    BooleanValue b = solver.impl().newBoolean(TheoryId::TrueFalse);
    VERIFY(b == true_literal);
    VERIFY(!b == false_literal);
    solver.impl().assignTrue(b, makeReason<ReasonKind::Always>({}));
    VERIFY(solver.impl().sat.propagate());
}

// -------------------------- Always reason -------------------------

bool SolverImpl::AlwaysReason::testReason(Solver&, BooleanValue, const Reason&) {
    return true;
}

ClauseAndIndex SolverImpl::AlwaysReason::reasonToClause(Solver& solver, BooleanValue lit, const Reason&) {
    auto& clause = solver.scratchClause();
    clause.push_back(lit);
    return { clause, 0 };
}

// ------------------------- Decision reason ------------------------

bool SolverImpl::DecisionReason::testReason(Solver&, BooleanValue, const Reason&) {
    return false;
}

ClauseAndIndex SolverImpl::DecisionReason::reasonToClause(Solver&, BooleanValue, const Reason&) {
    // A decision cannot be justified
    VERIFY_NOT_REACHED();
}

// ----------------------- Rewrite equalities -----------------------

SolverImpl::RewriteEqualities::RewriteEqualities(Solver& solver)
    : RewriteEqualities(solver, make_int_sequence<(int_t)ValueKind::COUNT - 1>()) { }

template<int_t... kinds>
SolverImpl::RewriteEqualities::RewriteEqualities(Solver& solver, int_sequence<kinds...>)
    : m_rwes { RewriteEquality(solver, ValueKind(kinds + 1), equalityTheoryFor(ValueKind(kinds + 1)))... } { }

bool SolverImpl::RewriteEqualities::testReason(Solver& solver, BooleanValue literal, const Reason& reason) {
    return (*this)[valueKindOfEqualityTheory(literal.theory())].testReason(solver, literal, reason);
}

ClauseAndIndex SolverImpl::RewriteEqualities::reasonToClause(Solver& solver, BooleanValue literal, const Reason& reason) {
    return (*this)[valueKindOfEqualityTheory(literal.theory())].reasonToClause(solver, literal, reason);
}

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

#define EQUALITY_THEORY(valueKind, ...) impl.rewriteEqualities[ValueKind::valueKind].newDecisionLevel(impl);
#include <verify/backend/theories.inc>
}

void SatCore::Interface::onBacktrack() {
    auto& impl = static_cast<SolverImpl&>(*this);

#define EQUALITY_THEORY(valueKind, ...) impl.rewriteEqualities[ValueKind::valueKind].backtrack(impl);
#include <verify/backend/theories.inc>
}

bool SatCore::Interface::testReason(Literal lit, const Reason& reason) {
    auto& impl = static_cast<SolverImpl&>(*this);
#define REASON(name, data, propagating, implMember) \
    case ReasonKind::name:                          \
        return impl.implMember.testReason(impl, lit, reason);
    switch (reason.kind()) {
#include <verify/backend/reasons.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

ClauseAndIndex SatCore::Interface::reasonToClause(Literal lit, const Reason& reason) {
    auto& impl = static_cast<SolverImpl&>(*this);
#define REASON(name, data, propagating, implMember) \
    case ReasonKind::name:                          \
        return impl.implMember.reasonToClause(impl, lit, reason);
    switch (reason.kind()) {
#include <verify/backend/reasons.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

void SatCore::Interface::propagateAssignment(Literal lit) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.clauses.propagateAssignment(impl, lit);

    switch (lit.theory()) {

#define EQUALITY_THEORY(valueKind)                                             \
    case TheoryId::valueKind##Equality: {                                      \
        auto& rwe = impl.rewriteEqualities[ValueKind::valueKind];              \
        auto pair = decodePairTheoryValue<TheoryId::valueKind##Equality>(lit); \
        if (lit.negated())                                                     \
            rwe.applyDisequal(impl, pair, true);                               \
        else                                                                   \
            rwe.applyEqual(impl, pair, true);                                  \
        break;                                                                 \
    }
#include <verify/backend/theories.inc>

    default:
        break;
    }
}

void SatCore::Interface::unapplyAssignment(Literal lit) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.clauses.unapplyAssignment(impl, lit);

    switch (lit.theory()) {
    default:
        break;
    }
}

void SatCore::Interface::reapplyAssignment(Literal lit) {
    auto& impl = static_cast<SolverImpl&>(*this);

    switch (lit.theory()) {

#define EQUALITY_THEORY(valueKind)                                             \
    case TheoryId::valueKind##Equality: {                                      \
        auto& rwe = impl.rewriteEqualities[ValueKind::valueKind];              \
        auto pair = decodePairTheoryValue<TheoryId::valueKind##Equality>(lit); \
        if (lit.negated())                                                     \
            rwe.applyDisequal(impl, pair, false);                              \
        else                                                                   \
            rwe.applyEqual(impl, pair, false);                                 \
        break;                                                                 \
    }
#include <verify/backend/theories.inc>

    default:
        break;
    }
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

// -------------------------- Rewrite order -------------------------

std::strong_ordering Solver::rewriteOrder(Value a, Value b) {
    auto theoryOrdering = a.theory() <=> b.theory();
    if (theoryOrdering != 0)
        return theoryOrdering;

    switch (a.theory()) {
    case TheoryId::TrueFalse:
    case TheoryId::ClauseGlueVariables:
    case TheoryId::AuxBooleanVariables:
    case TheoryId::AuxUninterpretedConstants:
        return a.id() <=> b.id();

#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair)                                     \
    case TheoryId::name: {                                                                                   \
        auto pairOrdering = impl().pairLabelOf<TheoryId::name>(a) <=> impl().pairLabelOf<TheoryId::name>(b); \
        if (pairOrdering != 0)                                                                               \
            return pairOrdering;                                                                             \
        return a.id() <=> b.id();                                                                            \
    }
#include <verify/backend/theories.inc>

    default:
        VERIFY_NOT_REACHED();
    }
}

// ------------------------------ Pairs -----------------------------

uint32_t PairSet::get(Solver& solver, Pair pair) {
    return Base::get(solver, pair);
}

std::strong_ordering PairSet::compare(Solver& solver, Pair a, Pair b) {
    auto targetOrdering = solver.rewriteOrder(a.target, b.target);
    if (targetOrdering != 0)
        return targetOrdering;
    return solver.rewriteOrder(a.source, b.source);
}

uint32_t PairSet::makeNode(Solver&, Pair pair, TreeLabel label) {
    PairHandle handle(kindOf(pair.source.theory()), nextNodeHandle());
    return Base::makeNode(label, pair);
}

void SolverImpl::onNewPair(PairHandle handle) {
    VERIFY(!handle.specialPair());
    VERIFY(handle.valueKind() != ValueKind::Boolean); // Bools have their own version of this function.
#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair)       \
    if (handle.valueKind() == ValueKind::pairValueKind) {                      \
        VERIFY(valueCount(TheoryId::name) == handle.pairId() * valuesPerPair); \
        data.newValue(TheoryId::name, valuesPerPair);                          \
    }
#include <verify/backend/theories.inc>

    rewriteEqualities[handle.valueKind()].newPair(*this, handle);
}

void SolverImpl::onNewBooleanPair(PairHandle handle) {
    VERIFY(!handle.specialPair());
    VERIFY(handle.valueKind() == ValueKind::Boolean);

    BooleanValue newBool = newBoolean(TheoryId::BooleanEquality);

    /*
    Equalities are eagerly encoded as clauses.
    For each equality there will be 4 clauses:
        a == b ||  a ||  b
        a == b || !a || !b
        a != b ||  a || !b
        a != b || !a ||  b
    */
    auto [a, b] = at(handle);
    auto lit = (BooleanValue)encodePairTheoryValue<TheoryId::BooleanEquality>(handle);
    VERIFY(lit == newBool);
    addClause({ lit, (BooleanValue)a, (BooleanValue)b });
    addClause({ lit, !(BooleanValue)a, !(BooleanValue)b });
    addClause({ !lit, (BooleanValue)a, !(BooleanValue)b });
    addClause({ !lit, !(BooleanValue)a, (BooleanValue)b });
}

PairHandle Solver::findPair(Value a, Value b) {
    VERIFY(a != b);
    ValueKind valueKind = kindOf(a.theory());
    VERIFY(valueKind == kindOf(b.theory()));
    if (rewriteOrder(a, b) > 0)
        std::swap(a, b);

    if (valueKind == ValueKind::Boolean) {
        if (BooleanValue(a).negated()) {
            a = !(BooleanValue)a;
            b = !(BooleanValue)b;
        }
        if (a == true_literal)
            return PairHandle(b);
    }

    return findPair({ a, b });
}

PairHandle Solver::findPair(Pair p) {
    ValueKind valueKind = kindOf(p.source.theory());
    auto& pairs = impl().pairs[std::to_underlying(valueKind)];
    int_t oldSize = pairs.size();

    if (valueKind == ValueKind::Boolean) {
        bool negated = BooleanValue(p.target).negated();
        p.target = BooleanValue(p.target).baseValue();
        uint32_t idx = pairs.get(*this, p);
        if (pairs.size() != oldSize) {
            impl().onNewBooleanPair(PairHandle { valueKind, idx * 2u });
        }
        return PairHandle { valueKind, idx * 2u + (negated ? 1u : 0u) };
    }

    uint32_t id = pairs.get(*this, p);
    PairHandle handle { valueKind, id };
    if (pairs.size() != oldSize)
        impl().onNewPair(handle);
    return handle;
}

Pair Solver::at(PairHandle handle) {
    if (handle.specialPair()) {
        auto b = handle.encodedValue();
        VERIFY(kindOf(b.theory()) == ValueKind::Boolean);
        return { true_literal, b };
    }

    auto& pairs = impl().pairs[std::to_underlying(handle.valueKind())];
    if (handle.valueKind() == ValueKind::Boolean) {
        uint32_t idx = handle.pairId() / 2;
        bool targetNegated = (handle.pairId() & 1u) != 0u;
        Pair pair = pairs.at(idx);
        if (targetNegated)
            pair.target = !(BooleanValue)pair.target;
        return pair;
    }

    return pairs.at(handle.pairId());
}

template<TheoryId theory>
uint64_t SolverImpl::pairLabelOf(Value v) {
    static constexpr ValueKind kind = kindOf(theory);

    auto& pairs = this->pairs[std::to_underlying(kind)];
    PairHandle handle = decodePairTheoryValue<theory>(v);
    if constexpr (theory == TheoryId::BooleanEquality) {
        uint32_t idx = handle.pairId() / 2;
        bool negated = (handle.pairId() & 1u) != 0u;
        return (uint64_t)pairs.label(idx) * 2 + (negated ? 1 : 0);
    } else {
        return pairs.label(handle.pairId());
    }
}

// ---------------------------- Equality ----------------------------

bool Solver::alwaysDisequal(Value a, Value b) {
    ValueKind valueKind = kindOf(a.theory());
    VERIFY(valueKind == kindOf(b.theory()));
    switch (valueKind) {
    case ValueKind::Boolean:
        if (a == !(BooleanValue)b)
            return true;
        return false;
    default:
        return false;
    }
}

BooleanValue Solver::equality(Value a, Value b) {
    if (a == b)
        return true_literal;
    if (alwaysDisequal(a, b))
        return false_literal;
    return equality(findPair(a, b));
}

BooleanValue Solver::equality(PairHandle handle) {
    if (handle.specialPair()) {
        auto b = handle.encodedValue();
        VERIFY(kindOf(b.theory()) == ValueKind::Boolean);
        // Encodes true == b which is equivalent to b
        return (BooleanValue)b;
    }

    if (handle.valueKind() == ValueKind::Boolean) {
        return (BooleanValue)encodePairTheoryValue<TheoryId::BooleanEquality>(handle);
    } else {
        return impl().rewriteEqualities[handle.valueKind()].makeEquality(handle);
    }
}

bool Solver::assignedEqual(Value a, Value b) {
    auto valueKind = kindOf(a.theory());
    VERIFY(valueKind == kindOf(b.theory()));
    return impl().rewriteEqualities[valueKind].connected(a, b);
}

// ------------------------- Value factories ------------------------

Value SolverImpl::newValue(TheoryId theory) {
    return data.newValue(theory, 1);
}

BooleanValue SolverImpl::newBoolean(TheoryId theory) {
    VERIFY(kindOf(theory) == ValueKind::Boolean);
    BooleanValue result(data.newValue(theory, 2));
    return result;
}

BooleanValue Solver::newAuxBoolean(std::string name) {
    BooleanValue v = impl().newBoolean(TheoryId::AuxBooleanVariables);
    impl().auxBoolNames[v] = std::move(name);
    return v;
}

}