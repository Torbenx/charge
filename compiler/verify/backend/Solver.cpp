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
    , uninterpConstantEquality(*this, theory_params::eqUninterpretedConstant)
    , members(*this)
    , uninterpConstantSets(*this, theory_params::setsUninterpretedConstantSet) { }

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
    auto builder = solver.beginClause();
    builder.add(solver, lit);
    return { solver.viewClause(builder), 0 };
}

// ------------------------- Decision reason ------------------------

bool SolverImpl::DecisionReason::testReason(Solver&, BooleanValue, const Reason&) {
    return false;
}

ClauseAndIndex SolverImpl::DecisionReason::reasonToClause(Solver&, BooleanValue, const Reason&) {
    // A decision cannot be justified
    VERIFY_NOT_REACHED();
}

// ------------------------ SatCore forwards ------------------------

int_t Solver::currentDecisionLevel() const {
    return impl().sat.currentDecisionLevel();
}
void Solver::backtrack(int_t targetLevel) {
    impl().sat.beginBacktrack(targetLevel);
    impl().sat.endBacktrack();
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
ClauseBuilder Solver::beginClause() {
    return impl().sat.beginClause();
}
std::span<const BooleanValue> Solver::viewClause(const ClauseBuilder& builder) {
    return impl().sat.viewClause(builder);
}

// ------------------------ SatCore callbacks -----------------------

void SatCore::Interface::onNewDecisionLevel() {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.uninterpConstantEquality.newDecisionLevel(impl);
    impl.members.newDecisionLevel(impl);
}

void SatCore::Interface::onBeginBacktrack() {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.uninterpConstantEquality.beginBacktrack(impl);
    impl.members.beginBacktrack(impl);
}

void SatCore::Interface::onEndBacktrack() {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.uninterpConstantEquality.endBacktrack(impl);
    impl.members.endBacktrack(impl);
}

bool SatCore::Interface::testReason(Literal lit, const Reason& reason) {
    auto& impl = static_cast<SolverImpl&>(*this);
#define REASON(name, data, propagating, implMember) \
    case ReasonKind::name:                          \
        return impl.implMember.testReason(impl, lit, reason);
    switch (reason.kind()) {
#include <verify/backend/theories.inc>
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
#include <verify/backend/theories.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

void SatCore::Interface::propagateAssignment(Literal lit) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.clauses.propagateAssignment(impl, lit);

    switch (lit.theory()) {

#define UNINTERPRETED_EQUALITY_THEORY(valueKind, memberName)                         \
    case TheoryId::valueKind##Equality: {                                            \
        PairHandle pair = decodePairTheoryValue<TheoryId::valueKind##Equality>(lit); \
        if (lit.negated())                                                           \
            impl.memberName.propagateDisequal(impl, pair);                           \
        else                                                                         \
            impl.memberName.propagateEqual(impl, pair);                              \
        break;                                                                       \
    }
#define SET_THEORY(valueKind, memberName)                                            \
    case TheoryId::valueKind##Equality: {                                            \
        PairHandle pair = decodePairTheoryValue<TheoryId::valueKind##Equality>(lit); \
        if (!lit.negated())                                                          \
            impl.memberName.propagateEquality(impl, pair);                           \
        break;                                                                       \
    }                                                                                \
    case TheoryId::valueKind##ElementInSet:                                          \
        impl.memberName.propagateElementAssignment(impl, lit);                       \
        break;
#include <verify/backend/theories.inc>

    case TheoryId::MemberEquality: {
        PairHandle pair = decodePairTheoryValue<TheoryId::MemberEquality>(lit);
        if (!lit.negated())
            impl.members.propagateEqual(impl, pair);
        break;
    }
    default:
        break;
    }
}

void SatCore::Interface::unapplyAssignment(Literal lit) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.clauses.unapplyAssignment(impl, lit);

    switch (lit.theory()) {

#define SET_THEORY(valueKind, memberName)                    \
    case TheoryId::valueKind##ElementInSet:                  \
        impl.memberName.unapplyElementAssignment(impl, lit); \
        break;
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
    impl().uninterpConstantSets.refineClause(*this, clause);

    if (simplifyClause(*this, clause))
        return;

    if (clause.size() == 1) {
        assignTrue(clause[0], makeReason<ReasonKind::Always>({}));
        return;
    }

    impl().clauses.addClause(*this, clause);
}

void Solver::addClause(const ClauseBuilder& builder) {
    auto span = impl().sat.viewClause(builder);
    addClause({ span.begin(), span.end() });
}

// -------------------------- Data forwards -------------------------

int_t Solver::valueCount(TheoryId theory) {
    return impl().data.at(theory).valueCount;
}

int_t Solver::booleanCount(TheoryId theory) {
    VERIFY(kindOf(theory) == ValueKind::Boolean);
    return valueCount(theory) / 2;
}

// ------------------------ Members forwards ------------------------

Member Solver::composeMembers(std::span<const Member> expr) {
    return impl().members.compose(*this, expr);
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
    case TheoryId::MemberLiterals:
    case TheoryId::AuxMemberVariables:
    case TheoryId::AuxUninterpretedConstantSets:
    case TheoryId::UninterpretedConstantSetEmptySet:
        return a.id() <=> b.id();

    case TheoryId::CompositeMembers: {
        auto& ms = impl().members;
        return ms.compositeLabel((Member)a) <=> ms.compositeLabel((Member)b);
    }

    case TheoryId::UninterpretedConstantSetElementInSet:
    case TheoryId::UninterpretedConstantSetExpressions:
        // Ordering is not important since not rewriting is done
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

    if (handle.valueKind() == ValueKind::UninterpretedConstant) {
        uninterpConstantEquality.newPair(*this, handle);
    } else if (handle.valueKind() == ValueKind::Member) {
        members.newPair(*this, handle);
    } else if (handle.valueKind() == ValueKind::UninterpretedConstantSet) {
        uninterpConstantSets.newPair(*this, handle);
    }
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
            return PairHandle::makeSpecialPair(b);
    } else if (valueKind == ValueKind::UninterpretedConstantSet) {
        Value emptySet = impl().uninterpConstantSets.emptySet();
        if (a == emptySet)
            return PairHandle::makeSpecialPair(b);
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
        ValueKind kind = kindOf(b.theory());
        if (kind == ValueKind::Boolean)
            return { true_literal, b };
        else if (kind == ValueKind::UninterpretedConstantSet)
            return { impl().uninterpConstantSets.emptySet(), b };
        else
            VERIFY_NOT_REACHED();
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
    if (handle.valueKind() == ValueKind::Boolean) {
        if (handle.specialPair()) {
            // Encodes true == b which is equivalent to b
            return (BooleanValue)handle.encodedValue();
        }
        return (BooleanValue)encodePairTheoryValue<TheoryId::BooleanEquality>(handle);
    } else if (handle.valueKind() == ValueKind::UninterpretedConstant) {
        VERIFY(!handle.specialPair());
        return impl().uninterpConstantEquality.makeEquality(handle);
    } else if (handle.valueKind() == ValueKind::Member) {
        VERIFY(!handle.specialPair());
        return impl().members.makeEquality(handle);
    } else if (handle.valueKind() == ValueKind::UninterpretedConstantSet) {
        if (handle.specialPair()) {
            // Encodes emptySet == b which is equivalent to b being empty
            return impl().uninterpConstantSets.makeIsEmpty(*this, handle.encodedValue());
        }
        return impl().uninterpConstantSets.makeEquality(handle);
    } else {
        VERIFY_NOT_REACHED();
    }
}

bool Solver::assignedEqual(Value a, Value b) {
    auto valueKind = kindOf(a.theory());
    VERIFY(valueKind == kindOf(b.theory()));
    if (valueKind == ValueKind::Boolean) {
        return (assignedTrue((BooleanValue)a) && assignedTrue((BooleanValue)b))
            || (assignedFalse((BooleanValue)a) && assignedFalse((BooleanValue)b));
    } else if (valueKind == ValueKind::UninterpretedConstant) {
        return impl().uninterpConstantEquality.rewrite(a) == impl().uninterpConstantEquality.rewrite(b);
    } else if (valueKind == ValueKind::Member) {
        return impl().members.rewrite((Member)a) == impl().members.rewrite((Member)b);
    } else {
        return assignedTrue(equality(a, b));
    }
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

BooleanValue Solver::newAuxBooleanVariable() {
    return impl().newBoolean(TheoryId::AuxBooleanVariables);
}

Value Solver::newAuxUninterpretedConstant() {
    return impl().newValue(TheoryId::AuxUninterpretedConstants);
}

Value Solver::newAuxUninterpretedConstantSet() {
    return impl().newValue(TheoryId::AuxUninterpretedConstantSets);
}

Member Solver::newAuxMemberVariable() {
    return (Member)impl().newValue(TheoryId::AuxMemberVariables);
}

}