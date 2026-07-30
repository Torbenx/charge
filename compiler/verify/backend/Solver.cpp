#include <verify/backend/SolverImpl.h>

namespace verify::backend {

// --------------------------- Constructor --------------------------

std::unique_ptr<Solver> Solver::make() {
    return std::make_unique<SolverImpl>();
}

Solver::Solver() { }

SolverImpl::SolverImpl()
    : literalInfos(*this, Sort::Boolean)
    , clauses(*this)
    , builtinTrueFalse(*this)
    , uninterpConstantEquality(*this, theory_params::eqUninterpretedConstant)
    , members(*this)
    , uninterpConstantSets(*this, theory_params::setsUninterpretedConstantSet)
    , uninterpConstantSingletons(*this,
          {
              .elementSort = Sort::UninterpretedConstant,
              .setSort = Sort::UninterpretedConstantSet,
              .singletonTheory = TheoryId::UninterpretedConstantSingletonSets,
              .inSingletonReason = makeTypedReasonKind<ReasonKind::UninterpretedConstantInSingleton>(),
          })
    , memoryDeclarationEquality(*this, theory_params::eqMemoryDeclaration)
    , memorySets(*this, theory_params::setsMemoryLocationSet)
    , memoryLocationSets(*this) { }

SolverImpl::BuiltinTrueFalse::BuiltinTrueFalse(Solver& solver) {
    Bool b = solver.impl().newBoolean(TheoryId::TrueFalse);
    VERIFY(b == true_literal);
    VERIFY(!b == false_literal);
    solver.impl().assignTrue(b, makeReason<ReasonKind::Always>({}));
    VERIFY(solver.impl().sat.propagate());
}

// -------------------------- Always reason -------------------------

bool SolverImpl::AlwaysReason::testReason(Solver&, Bool, const Reason&) {
    return true;
}

ClauseAndIndex SolverImpl::AlwaysReason::reasonToClause(Solver& solver, Bool lit, const Reason&) {
    auto builder = solver.beginClause();
    builder.add(solver, lit);
    return { solver.viewClause(builder), 0 };
}

// ------------------------- Decision reason ------------------------

bool SolverImpl::DecisionReason::testReason(Solver&, Bool, const Reason&) {
    return false;
}

ClauseAndIndex SolverImpl::DecisionReason::reasonToClause(Solver&, Bool, const Reason&) {
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
bool Solver::assignedTrue(Bool lit) {
    return impl().sat.assignedTrue(lit);
}
bool Solver::assignedFalse(Bool lit) {
    return impl().sat.assignedFalse(lit);
}
void Solver::decideTrue(Bool literal) {
    impl().sat.decideTrue(literal);
}
void Solver::assignTrue(Bool trueLit, const Reason& reason) {
    impl().sat.assignTrue(trueLit, reason);
}
bool Solver::alwaysTrue(Bool value) {
    return impl().sat.alwaysTrue(value);
}
ClauseBuilder Solver::beginClause() {
    return impl().sat.beginClause();
}
std::span<const Bool> Solver::viewClause(const ClauseBuilder& builder) {
    return impl().sat.viewClause(builder);
}

// ------------------------ SatCore callbacks -----------------------

void SatCore::Interface::onNewDecisionLevel() {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.uninterpConstantEquality.newDecisionLevel(impl);
    impl.memoryDeclarationEquality.newDecisionLevel(impl);
    impl.members.newDecisionLevel(impl);
    impl.memoryLocationSets.newDecisionLevel(impl);
    impl.uninterpConstantSingletons.newDecisionLevel(impl);
}

void SatCore::Interface::onBeginBacktrack() {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.uninterpConstantEquality.beginBacktrack(impl);
    impl.memoryDeclarationEquality.beginBacktrack(impl);
    impl.members.beginBacktrack(impl);
    // Must run after members so that the prefix index observes the restored rewrites
    impl.memoryLocationSets.beginBacktrack(impl);
    impl.uninterpConstantSingletons.beginBacktrack(impl);
}

void SatCore::Interface::onEndBacktrack() {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.uninterpConstantEquality.endBacktrack(impl);
    impl.memoryDeclarationEquality.endBacktrack(impl);
    impl.members.endBacktrack(impl);
    impl.memoryLocationSets.endBacktrack(impl);
    impl.uninterpConstantSingletons.endBacktrack(impl);
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

#define UNINTERPRETED_EQUALITY_THEORY(sort, memberName)                         \
    case TheoryId::sort##Equality: {                                            \
        PairHandle pair = decodePairTheoryValue<TheoryId::sort##Equality>(lit); \
        if (lit.negated())                                                      \
            impl.memberName.propagateDisequal(impl, pair);                      \
        else                                                                    \
            impl.memberName.propagateEqual(impl, pair);                         \
        break;                                                                  \
    }
#define SET_THEORY(sort, memberName)                                            \
    case TheoryId::sort##Equality: {                                            \
        PairHandle pair = decodePairTheoryValue<TheoryId::sort##Equality>(lit); \
        if (!lit.negated())                                                     \
            impl.memberName.propagateEquality(impl, pair);                      \
        break;                                                                  \
    }                                                                           \
    case TheoryId::sort##ElementInSet:                                          \
        impl.memberName.propagateElementAssignment(impl, lit);                  \
        break;
#include <verify/backend/theories.inc>

    case TheoryId::MemberEquality: {
        PairHandle pair = decodePairTheoryValue<TheoryId::MemberEquality>(lit);
        if (!lit.negated()) {
            impl.members.propagateEqual(impl, pair);
            impl.memoryLocationSets.propagateMemberRewrites(impl);
        }
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

#define SET_THEORY(sort, memberName)                         \
    case TheoryId::sort##ElementInSet:                       \
        impl.memberName.unapplyElementAssignment(impl, lit); \
        break;
#include <verify/backend/theories.inc>

    default:
        break;
    }
}

void SatCore::Interface::learnClause(std::vector<Bool> clause) {
    auto& impl = static_cast<SolverImpl&>(*this);
    impl.addClause(std::move(clause));
}

// ------------------------------ Uses ------------------------------

void SolverImpl::propagateRewrite(Use use) {
#define USE_KIND(name, implMember)        \
    case UseKind::name:                   \
        implMember.propagateRewrite(*this, use); \
        break;
    switch (use.kind()) {
#include <verify/backend/theories.inc>

    default:
        VERIFY_NOT_REACHED();
    }
}

// ------------------------------ Sets ------------------------------

Sets& SolverImpl::setTheory(Sort sort) {
    static constexpr auto table = [] {
        std::array<Sets SolverImpl::*, std::to_underlying(Sort::COUNT)> result;
        result.fill(nullptr);
#define SET_THEORY(sort, memberName) result[std::to_underlying(Sort::sort)] = &SolverImpl::memberName;
#include <verify/backend/theories.inc>
        return result;
    }();
    return (*this).*(table[std::to_underlying(sort)]);
}

void SolverImpl::propagateSetContainment(Sets&, Sets::ElementId element, Sets::Containment containment) {
    switch (containment.set().theory()) {
    case TheoryId::UninterpretedConstantSingletonSets:
        uninterpConstantSingletons.propagateContainment(*this, element, containment);
        break;
    case TheoryId::MemoryLocationSets:
        memoryLocationSets.propagateContainment(*this, element, containment);
        break;
    default:
        break;
    }
}

bool SolverImpl::setAlwaysNonEmpty(Value set) {
    switch (sortOf(set.theory())) {
    case Sort::UninterpretedConstantSet:
        return set.theory() == TheoryId::UninterpretedConstantSingletonSets;
    default:
        return false;
    }
}

// ------------------------ Clauses forwards ------------------------

static bool simplifyClause(Solver& solver, std::vector<Bool>& clause) {
    bool alwaysTrue = false;
    auto newEnd = std::partition(clause.begin(), clause.end(), [&](Bool lit) {
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

void Solver::addClause(std::vector<Bool> clause) {
#define SET_THEORY(sort, memberName) impl().memberName.refineClause(*this, clause);
#include <verify/backend/theories.inc>

    if (simplifyClause(*this, clause))
        return;

    if (clause.size() == 1) {
        assignTrue(clause[0], makeReason<ReasonKind::Always>({}));
        return;
    }

    impl().clauses.addClause(*this, std::move(clause));
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
    VERIFY(sortOf(theory) == Sort::Boolean);
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
    case TheoryId::AuxMemoryDeclarationVariables:
        return a.id() <=> b.id();

    case TheoryId::CompositeMembers: {
        auto& ms = impl().members;
        return ms.compositeLabel((Member)a) <=> ms.compositeLabel((Member)b);
    }

    case TheoryId::UninterpretedConstantSingletonSets: {
        auto& singletons = impl().uninterpConstantSingletons;
        return rewriteOrder(singletons.element(a), singletons.element(b));
    }
    case TheoryId::MemoryLocationSets: {
        auto& locations = impl().memoryLocationSets;
        MemoryLocation locA = locations.locationOf(a);
        MemoryLocation locB = locations.locationOf(b);
        auto declarationOrdering = rewriteOrder(locA.declaration, locB.declaration);
        if (declarationOrdering != 0)
            return declarationOrdering;
        return rewriteOrder(locA.member, locB.member);
    }

    // Ordering is not important for these since no rewriting is done
#define SET_THEORY(sort, memberName)   \
    case TheoryId::sort##EmptySet:     \
    case TheoryId::sort##ElementInSet: \
    case TheoryId::sort##Expressions:  \
        return a.id() <=> b.id();
#include <verify/backend/theories.inc>

#define PAIR_THEORY(name, theorySort, pairSort, valuesPerPair)                                               \
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
    VERIFY(handle.sort() != Sort::Boolean); // Bools have their own version of this function.
#define PAIR_THEORY(name, theorySort, pairSort, valuesPerPair)                 \
    if (handle.sort() == Sort::pairSort) {                                     \
        VERIFY(valueCount(TheoryId::name) == handle.pairId() * valuesPerPair); \
        data.newValue(TheoryId::name, valuesPerPair);                          \
    }
#include <verify/backend/theories.inc>

    Sort sort = handle.sort();
    auto [a, b] = at(handle);
    if (sort == Sort::UninterpretedConstant) {
        // Note: This is a lot of stuff to add eagerly, maybe this can be reduced in the future.
        uninterpConstantEquality.newPair(*this, handle);
        Bool elementEq = equality(handle);
        Bool singletonEq = equality(uninterpConstantSingletons.singleton(*this, a), uninterpConstantSingletons.singleton(*this, b));
        addClause({ elementEq, !singletonEq });
        addClause({ !elementEq, singletonEq });
    } else if (sort == Sort::Member) {
        members.newPair(*this, handle);
        memoryLocationSets.propagateMemberRewrites(*this);
    } else if (sort == Sort::MemoryDeclaration) {
        memoryDeclarationEquality.newPair(*this, handle);
    } else if (isSetSort(sort)) {
        setTheory(sort).newPair(*this, handle);
        if (a.theory() == TheoryId::UninterpretedConstantSingletonSets && b.theory() == TheoryId::UninterpretedConstantSingletonSets) {
            // The onNewPair() call for the element equality will automatically create the equivalence clauses.
            [[maybe_unused]] Bool elementEq = equality(uninterpConstantSingletons.element(a), uninterpConstantSingletons.element(b));
        }
    }
}

void SolverImpl::onNewBooleanPair(PairHandle handle) {
    VERIFY(!handle.specialPair());
    VERIFY(handle.sort() == Sort::Boolean);

    Bool newBool = newBoolean(TheoryId::BooleanEquality);

    /*
    Equalities are eagerly encoded as clauses.
    For each equality there will be 4 clauses:
        a == b ||  a ||  b
        a == b || !a || !b
        a != b ||  a || !b
        a != b || !a ||  b
    */
    auto [a, b] = at(handle);
    auto lit = (Bool)encodePairTheoryValue<TheoryId::BooleanEquality>(handle);
    VERIFY(lit == newBool);
    addClause({ lit, (Bool)a, (Bool)b });
    addClause({ lit, !(Bool)a, !(Bool)b });
    addClause({ !lit, (Bool)a, !(Bool)b });
    addClause({ !lit, !(Bool)a, (Bool)b });
}

PairHandle Solver::findPair(Value a, Value b) {
    VERIFY(a != b);
    Sort sort = sortOf(a.theory());
    VERIFY(sort == sortOf(b.theory()));
    if (rewriteOrder(a, b) > 0)
        std::swap(a, b);

    if (sort == Sort::Boolean) {
        if (Bool(a).negated()) {
            a = !(Bool)a;
            b = !(Bool)b;
        }
        if (a == true_literal)
            return PairHandle::makeSpecialPair(b);
    } else if (isSetSort(sort)) {
        Value emptySet = impl().setTheory(sort).emptySet();
        if (a == emptySet)
            return PairHandle::makeSpecialPair(b);
    }

    return findPair({ a, b });
}

PairHandle Solver::findPair(Pair p) {
    Sort sort = sortOf(p.source.theory());
    auto& pairs = impl().pairs[std::to_underlying(sort)];
    int_t oldSize = pairs.size();

    if (sort == Sort::Boolean) {
        bool negated = Bool(p.target).negated();
        p.target = Bool(p.target).baseValue();
        uint32_t idx = pairs.get(*this, p);
        if (pairs.size() != oldSize) {
            impl().onNewBooleanPair(PairHandle { sort, idx * 2u });
        }
        return PairHandle { sort, idx * 2u + (negated ? 1u : 0u) };
    }

    uint32_t id = pairs.get(*this, p);
    PairHandle handle { sort, id };
    if (pairs.size() != oldSize)
        impl().onNewPair(handle);
    return handle;
}

Pair Solver::at(PairHandle handle) {
    if (handle.specialPair()) {
        auto b = handle.encodedValue();
        Sort sort = sortOf(b.theory());
        if (sort == Sort::Boolean)
            return { true_literal, b };
        else if (isSetSort(sort))
            return { impl().setTheory(sort).emptySet(), b };
        else
            VERIFY_NOT_REACHED();
    }

    auto& pairs = impl().pairs[std::to_underlying(handle.sort())];
    if (handle.sort() == Sort::Boolean) {
        uint32_t idx = handle.pairId() / 2;
        bool targetNegated = (handle.pairId() & 1u) != 0u;
        Pair pair = pairs.at(idx);
        if (targetNegated)
            pair.target = !(Bool)pair.target;
        return pair;
    }

    return pairs.at(handle.pairId());
}

template<TheoryId theory>
uint64_t SolverImpl::pairLabelOf(Value v) {
    static constexpr Sort sort = sortOf(theory);

    auto& pairs = this->pairs[std::to_underlying(sort)];
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
    Sort sort = sortOf(a.theory());
    VERIFY(sort == sortOf(b.theory()));
    switch (sort) {
    case Sort::Boolean:
        if (a == !(Bool)b)
            return true;
        return false;
    default:
        return false;
    }
}

Bool Solver::equality(Value a, Value b) {
    if (a == b)
        return true_literal;
    if (alwaysDisequal(a, b))
        return false_literal;
    return equality(findPair(a, b));
}

Bool Solver::equality(PairHandle handle) {
    if (handle.sort() == Sort::Boolean) {
        if (handle.specialPair()) {
            // Encodes true == b which is equivalent to b
            return (Bool)handle.encodedValue();
        }
        return (Bool)encodePairTheoryValue<TheoryId::BooleanEquality>(handle);
    } else if (handle.sort() == Sort::UninterpretedConstant) {
        VERIFY(!handle.specialPair());
        return impl().uninterpConstantEquality.makeEquality(handle);
    } else if (handle.sort() == Sort::Member) {
        VERIFY(!handle.specialPair());
        return impl().members.makeEquality(handle);
    } else if (handle.sort() == Sort::MemoryDeclaration) {
        VERIFY(!handle.specialPair());
        return impl().memoryDeclarationEquality.makeEquality(handle);
    } else if (isSetSort(handle.sort())) {
        Sets& sets = impl().setTheory(handle.sort());
        if (handle.specialPair()) {
            // Encodes emptySet == b which is equivalent to b being empty
            return sets.isEmpty(*this, handle.encodedValue());
        }
        return sets.makeEquality(handle);
    } else {
        VERIFY_NOT_REACHED();
    }
}

bool Solver::assignedEqual(Value a, Value b) {
    auto sort = sortOf(a.theory());
    VERIFY(sort == sortOf(b.theory()));
    if (sort == Sort::Boolean) {
        return (assignedTrue((Bool)a) && assignedTrue((Bool)b))
            || (assignedFalse((Bool)a) && assignedFalse((Bool)b));
    } else if (sort == Sort::UninterpretedConstant) {
        return impl().uninterpConstantEquality.rewrite(a) == impl().uninterpConstantEquality.rewrite(b);
    } else if (sort == Sort::Member) {
        return impl().members.rewrite((Member)a) == impl().members.rewrite((Member)b);
    } else if (sort == Sort::MemoryDeclaration) {
        return impl().memoryDeclarationEquality.rewrite(a) == impl().memoryDeclarationEquality.rewrite(b);
    } else {
        return assignedTrue(equality(a, b));
    }
}

// ------------------------- Value factories ------------------------

Value SolverImpl::newValue(TheoryId theory) {
    return data.newValue(theory, 1);
}

Bool SolverImpl::newBoolean(TheoryId theory) {
    VERIFY(sortOf(theory) == Sort::Boolean);
    Bool result(data.newValue(theory, 2));
    return result;
}

Bool Solver::newAuxBooleanVariable() {
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

MemoryDeclaration Solver::newAuxMemoryDeclarationVariable() {
    return (MemoryDeclaration)impl().newValue(TheoryId::AuxMemoryDeclarationVariables);
}

}