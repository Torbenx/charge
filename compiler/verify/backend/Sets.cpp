#include <verify/backend/Sets.h>

#include <verify/backend/SolverImpl.h>

#include <algorithm>
#include <ranges>

namespace verify::backend {

Sets::Sets(
    Solver& solver,
    TheoryId expressionTheory,
    TheoryId emptySetTheory,
    TheoryId equalityTheory,
    TheoryId isEmptyTheory,
    TheoryId elementInSetTheory)
    : setKind(kindOf(expressionTheory))
    , expressionTheory(expressionTheory)
    , emptySetTheory(emptySetTheory)
    , equalityTheory(equalityTheory)
    , isEmptyTheory(isEmptyTheory)
    , elementInSetTheory(elementInSetTheory)
    , setInfos(solver, setKind)
    , inSetInfos(solver, elementInSetTheory)
    , isEmptyInfos(solver, isEmptyTheory) {
    Value empty = solver.impl().newValue(emptySetTheory);
    VERIFY(empty == emptySet());
    solver.assignTrue(makeIsEmpty(solver, empty), makeReason<ReasonKind::Always>({}));
    VERIFY(solver.impl().sat.propagate());
}

BooleanValue Sets::makeIsEmpty(Solver& solver, Value set) {
    auto& setInfo = setInfos[set];
    if (!setInfo.isEmptyLiteral.has_value()) {
        BooleanValue newLit = solver.impl().newBoolean(isEmptyTheory);
        setInfo.isEmptyLiteral = newLit;
        isEmptyInfos[newLit] = { .set = set };
    }
    return setInfo.isEmptyLiteral.value();
}

void Sets::newPair(Solver& solver, PairHandle pair) {
    VERIFY(!pair.specialPair());
    VERIFY(pair.valueKind() == setKind);
    auto [a, b] = solver.at(pair);
    solver.addClause({
        makeEquality(pair),
        !makeIsEmpty(solver, subset(solver, { a }, { b })),
        !makeIsEmpty(solver, subset(solver, { b }, { a })),
    });
    setInfos[a].equalities.push_back({ pair, b });
    setInfos[b].equalities.push_back({ pair, a });
}

bool Sets::unionExpression(Value value) const {
    return value.theory() == expressionTheory && !clauses[value.id()].front().contained();
}

bool Sets::subsetExpression(Value value) const {
    return value.theory() == expressionTheory && clauses[value.id()].front().contained();
}

Value Sets::union_(Solver& solver, std::span<const Value> sets) {
    uint32_t id = clauses.size();
    uint32_t clauseAttempt = nextClauseAttempt++;
    Value result = Value(expressionTheory, id);
    std::vector<Containment> clause;
    clause.push_back(!in(result));
    auto addSet = [this, &clause, clauseAttempt](Value set) {
        Containment literal = in(set);
        auto& info = infoFor(literal);
        if (info.inClause != clauseAttempt) {
            clause.push_back(in(set));
            info.inClause = clauseAttempt;
        }
    };
    for (Value set : sets) {
        VERIFY(kindOf(set.theory()) == setKind);
        if (set != emptySet())
            addSet(set);
    }
    if (clause.size() == 1) {
        return emptySet();
    } else if (clause.size() == 2) {
        return clause[1].set();
    } else {
        return addClause(solver, std::move(clause));
    }
}

Value Sets::subset(Solver& solver, std::span<const Value> intersection, std::span<const Value> minus) {
    VERIFY(intersection.size() >= 1);
    uint32_t id = clauses.size();
    uint32_t clauseAttempt = nextClauseAttempt++;
    Value result = Value(expressionTheory, id);
    std::vector<Containment> clause;
    clause.push_back(in(result));
    // For now assume no unions in 'intersection' and not subsets in 'minus'.
    auto addSet = [this, &clause, clauseAttempt](Containment c) { // !contained for intersections, contained for minus
        auto& info = infoFor(c);
        if (info.inClause != clauseAttempt) {
            clause.push_back(c);
            info.inClause = clauseAttempt;
            if (infoFor(!c).inClause == clauseAttempt) {
                return false;
            }
        }
        return true;
    };

    for (Value set : intersection) {
        if (set == emptySet())
            return emptySet();
        if (!addSet(!in(set)))
            return emptySet();
    }
    for (Value set : minus) {
        if (set == emptySet())
            continue;
        if (!addSet(in(set)))
            return emptySet();
    }
    VERIFY(clause.size() >= 2);
    if (clause.size() == 2) {
        VERIFY(!clause[1].contained()); // !contained indicates intersection
        return clause[1].set();
    } else {
        return addClause(solver, std::move(clause));
    }
}

bool Sets::ClauseHashEqual::operator()(const HashLookup& a, const HashEntry& b) const {
    return std::ranges::equal(std::views::drop(a.sets.clauses[b.expr.id()], 1), std::views::drop(a.clause, 1));
}

// https://www.boost.org/doc/libs/1_34_1/doc/html/boost/hash_combine.html
static void hash_combine(size_t& seed, size_t hash_value) {
    seed ^= hash_value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

static size_t hashClause(std::span<const Sets::Containment> clause) {
    size_t hash = 0xabcdef01;
    for (auto c : std::views::drop(clause, 1))
        hash_combine(hash, std::bit_cast<uint32_t>(c));
    return hash;
}

Value Sets::addClause(Solver& solver, std::vector<Containment> inClause) {
    VERIFY(inClause.size() >= 3);
    VERIFY(inClause.size() <= MAX_CLAUSE_SIZE);
    VERIFY((int_t)clauses.size() == solver.valueCount(expressionTheory));

    std::ranges::sort(std::views::drop(inClause, 1), [&solver](Containment a, Containment b) -> bool {
        if (a.contained() != b.contained())
            return !a.contained();
        return solver.rewriteOrder(a.set(), b.set()) < 0;
    });
    Value expressionValue(expressionTheory, clauses.size());
    size_t hash = hashClause(inClause);
    auto hashIt = clauseSet.find(HashLookup { hash, *this, inClause });
    if (hashIt != clauseSet.end()) {
        return hashIt->expr;
    }
    clauseSet.emplace(HashEntry { expressionValue, hash });

    clauses.emplace_back(std::move(inClause));
    auto& clause = clauses.back();

    VERIFY(solver.impl().newValue(expressionTheory) == expressionValue);
    VERIFY(clause.front().set() == expressionValue);
    for (int_t i = 0; i < (int_t)clause.size(); i++) {
        infoFor(clause[i]).occurrences.push_back({ .literalIndex = (uint32_t)i, .clauseIndex = expressionValue.id() });
    }

    for (int_t elementId = 0; elementId < (int_t)elements.size(); elementId++) {
        ElementId element(elementId);
        auto& masks = elements[element.id()].clauseMasks;
        VERIFY(masks.size() == clauses.size() - 1);
        // The first literal is for the just constructed set and cannot be assigned
        clause_mask_t newMask = Clauses::literalMask(0);
        for (int_t literalIndex = 1; literalIndex < (int_t)clause.size(); literalIndex++) {
            if (!assignedFalse(solver, element, clause[literalIndex]))
                newMask |= Clauses::literalMask(literalIndex);
        }
        masks.push_back(newMask);
        int popcnt = std::popcount(newMask);
        VERIFY(popcnt >= 1);
        if (popcnt == 1) {
            VERIFY(newMask == Clauses::literalMask(0));
            assignTrue(solver, element, clause[0],
                makeReason<ReasonKind::SetClauseExhaustive>({ .literalIndex = 0, .clauseIndex = expressionValue.id() }));
        }
    }

    return expressionValue;
}

void Sets::refineClause(Solver& solver, std::vector<BooleanValue>& boolClause) {
    auto setLits = std::ranges::partition(boolClause, [this](BooleanValue lit) {
        return lit.theory() != elementInSetTheory;
    });
    if (setLits.empty())
        return;

    // Generate set expressions, a separate one for each element
    std::vector<BooleanValue> replacementLiterals;
    std::ranges::sort(setLits, std::less(), [this](BooleanValue lit) { return inSetInfos[lit].element.id(); });
    for (auto it = setLits.begin(); it != setLits.end();) {
        std::vector<Containment> setClause;
        Value resultSet = Value(expressionTheory, clauses.size());
        setClause.push_back(in(resultSet));

        auto [clauseElement, firstCont] = mapFromBool(*it);
        setClause.push_back(firstCont);
        bool allPositive = firstCont.contained();
        for (;;) {
            ++it;
            if (it == setLits.end())
                break;
            auto [element, cont] = mapFromBool(*it);
            if (element != clauseElement)
                break;
            setClause.push_back(cont);
            if (!cont.contained())
                allPositive = false;
        }

        // All clauses contain at least one negative literal, a property that persists under resolution
        VERIFY(!allPositive);
        VERIFY(setClause.size() >= 2);
        if (setClause.size() == 2) {
            VERIFY(!setClause[1].contained());
            replacementLiterals.push_back(makeIsEmpty(solver, setClause[1].set()));
        } else {
            replacementLiterals.push_back(makeIsEmpty(solver, addClause(solver, std::move(setClause))));
        }
    }

    boolClause.erase(setLits.begin(), setLits.end());
    boolClause.append_range(replacementLiterals);
}

void Sets::assignTrue(Solver& solver, ElementId element, Containment lit, const Reason& reason) {
    solver.assignTrue(mapToBool(solver, element, lit), reason);
}

void Sets::decideTrue(Solver& solver, ElementId element, Containment lit) {
    solver.decideTrue(mapToBool(solver, element, lit));
}

bool Sets::assignedTrue(Solver& solver, ElementId element, Containment lit) {
    return solver.assignedTrue(mapToBool(solver, element, lit));
}

void Sets::propagateElementAssignment(Solver& solver, BooleanValue lit) {
    auto [element, cont] = mapFromBool(lit);
    propagateContainment(solver, element, cont);
}

void Sets::unapplyElementAssignment(Solver& solver, BooleanValue lit) {
    auto [element, cont] = mapFromBool(lit);
    unapplyContainment(solver, element, cont);
}

void Sets::propagateContainment(Solver& solver, ElementId element, Containment literal) {
    for (auto occ : infoFor(literal).occurrences) {
        const auto& clause = clauses[occ.clauseIndex];
        if (occ.literalIndex == 0) {
            // If the first literal in a clause is true than all other literals are false.
            for (Containment lit : std::views::drop(clause, 1))
                assignTrue(solver, element, !lit, makeReason<ReasonKind::SetClauseDefToExpr>({ .def = !literal, .expr = !lit }));
        } else {
            // If any literal in a clause other than the first is true the first literal is false.
            assignTrue(solver, element, !clause.front(), makeReason<ReasonKind::SetClauseExprToDef>({ .def = !clause.front(), .expr = !literal }));
        }
    }

    auto& clauseMasks = elements[element.id()].clauseMasks;
    for (auto occ : infoFor(!literal).occurrences) {
        // See also Clauses::propagateAssignment()
        clause_mask_t& clauseMask = clauseMasks[occ.clauseIndex];

        int popcnt = std::popcount(clauseMask);
        VERIFY((clauseMask & Clauses::literalMask(occ.literalIndex)) != (clause_mask_t)0);
        clauseMask &= ~Clauses::literalMask(occ.literalIndex);

        if (popcnt > 2)
            continue;
        VERIFY(popcnt == 2);

        // If all except one literal in a clause is false the last literal is true.
        const auto& clause = clauses[occ.clauseIndex];
        int_t trueLitIndex = std::countr_zero(clauseMask);
        LiteralOccurrence trueLitOcc = occ;
        trueLitOcc.literalIndex = trueLitIndex;
        assignTrue(solver, element, clause[trueLitIndex], makeReason<ReasonKind::SetClauseExhaustive>(trueLitOcc));
    }

    // Propagate equalities
    for (auto [pair, otherSet] : setInfos[literal.set()].equalities) {
        if (!solver.assignedTrue(makeEquality(pair)))
            continue;

        // Note: In a case where 'A = B' is true and 'in A' is propagated, we will obviously assign
        //      'in B'. And when 'in B' is propagated it will assign 'in A' again. This seems a bit
        //      redudant but I think is actually necessary for correctness in cases where 'in A' is
        //      reverted and 'in B' was learned in the meantime.
        Containment otherCont(otherSet, literal.contained());
        assignTrue(solver, element, otherCont, makeReason<ReasonKind::SetEqualityToElem>({ pair, literal }));
    }
}

void Sets::unapplyContainment(Solver&, ElementId element, Containment literal) {
    // See also Clauses::unapplyAssignment()
    auto& clauseMasks = elements[element.id()].clauseMasks;
    for (auto occ : infoFor(!literal).occurrences) {
        auto& clauseMask = clauseMasks[occ.clauseIndex];
        auto mask = Clauses::literalMask(occ.literalIndex);
        clauseMask |= mask;
    }
}

void Sets::propagateIsEmpty(Solver& solver, BooleanValue lit) {
    Value set = isEmptyInfos[lit].set;
    for (int_t elementId = 0; elementId < (int_t)elements.size(); elementId++) {
        ElementId element(elementId);
        assignTrue(solver, element, !in(set), makeReason<ReasonKind::SetEmptyToElem>({ lit }));
    }
}

void Sets::propagateEquality(Solver& solver, PairHandle pair) {
    auto [a, b] = solver.at(pair);
    for (int_t elementId = 0; elementId < (int_t)elements.size(); elementId++) {
        ElementId element(elementId);
        BooleanValue inA = mapToBool(solver, element, in(a));
        BooleanValue inB = mapToBool(solver, element, in(b));
        if (solver.assignedTrue(inA))
            solver.assignTrue(inB, makeReason<ReasonKind::SetEqualityToElem>({ pair, in(a) }));
        if (solver.assignedTrue(!inA))
            solver.assignTrue(!inB, makeReason<ReasonKind::SetEqualityToElem>({ pair, !in(a) }));
        if (solver.assignedTrue(inB))
            solver.assignTrue(inA, makeReason<ReasonKind::SetEqualityToElem>({ pair, in(b) }));
        if (solver.assignedTrue(!inB))
            solver.assignTrue(!inA, makeReason<ReasonKind::SetEqualityToElem>({ pair, !in(b) }));
    }
}

Sets::ElementId Sets::newElement(Solver& solver) {
    ElementId element(elements.size());
    elements.emplace_back();
    auto& masks = elements.back().clauseMasks;
    masks.resize(clauses.size(), (clause_mask_t)0);
    // Initially nothing is assigned
    for (int_t clauseIndex = 0; clauseIndex < (int_t)clauses.size(); clauseIndex++) {
        masks[clauseIndex] = Clauses::literalMask(clauses[clauseIndex].size()) - (clause_mask_t)1;
    }

    // Propagate empty sets
    solver.forEachBoolean(isEmptyTheory, [&](BooleanValue isEmptyLit) {
        if (solver.assignedTrue(isEmptyLit)) {
            auto& info = isEmptyInfos[isEmptyLit];
            assignTrue(solver, element, !in(info.set),
                makeReason<ReasonKind::SetEmptyToElem>({ isEmptyLit }));
        }
    });

    return element;
}

bool Sets::testReason(Solver& solver, BooleanValue boolLiteral, const Reason& reason) {
    auto [element, setLiteral] = inSetInfos[boolLiteral];
    switch (reason.kind()) {
    case ReasonKind::SetClauseDefToExpr:
        return assignedFalse(solver, element, reason.get<ReasonKind::SetClauseDefToExpr>().def);
    case ReasonKind::SetClauseExprToDef:
        return assignedFalse(solver, element, reason.get<ReasonKind::SetClauseExprToDef>().expr);
    case ReasonKind::SetClauseExhaustive: {
        auto occ = reason.get<ReasonKind::SetClauseExhaustive>();
        return std::popcount(elements[element.id()].clauseMasks[occ.clauseIndex]) == 1;
    }
    case ReasonKind::SetEqualityToElem: {
        auto [pair, source] = reason.get<ReasonKind::SetEqualityToElem>();
        return solver.assignedTrue(makeEquality(pair))
            && assignedTrue(solver, element, source);
    }
    case ReasonKind::SetEmptyToElem:
        return solver.assignedTrue(reason.get<ReasonKind::SetEmptyToElem>().isEmptyLiteral);
    default:
        VERIFY_NOT_REACHED();
    }
}

ClauseAndIndex Sets::reasonToClause(Solver& solver, BooleanValue boolLiteral, const Reason& reason) {
    auto [element, setLiteral] = inSetInfos[boolLiteral];
    ClauseBuilder result = solver.beginClause();
    switch (reason.kind()) {
    case ReasonKind::SetClauseDefToExpr:
        result.add(solver, boolLiteral);
        result.add(solver, mapToBool(solver, element, reason.get<ReasonKind::SetClauseDefToExpr>().def));
        return { solver.viewClause(result), 0 };
    case ReasonKind::SetClauseExprToDef:
        result.add(solver, boolLiteral);
        result.add(solver, mapToBool(solver, element, reason.get<ReasonKind::SetClauseExprToDef>().expr));
        return { solver.viewClause(result), 0 };
    case ReasonKind::SetClauseExhaustive: {
        auto occ = reason.get<ReasonKind::SetClauseExhaustive>();
        for (Containment lit : clauses[occ.clauseIndex]) {
            result.add(solver, mapToBool(solver, element, lit));
        }
        return { solver.viewClause(result), occ.literalIndex };
    }
    case ReasonKind::SetEqualityToElem: {
        auto [pair, assignSource] = reason.get<ReasonKind::SetEqualityToElem>();
        result.add(solver, boolLiteral);
        result.add(solver, mapToBool(solver, element, !assignSource));
        result.add(solver, !makeEquality(pair));
        return { solver.viewClause(result), 0 };
    }
    case ReasonKind::SetEmptyToElem: {
        result.add(solver, boolLiteral);
        result.add(solver, !reason.get<ReasonKind::SetEmptyToElem>().isEmptyLiteral);
        return { solver.viewClause(result), 0 };
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

BooleanValue Sets::mapToBool(Solver& solver, ElementId element, Containment literal) {
    auto& inSetLiterals = setInfos[literal.set()].elementInSetLiterals;
    if (inSetLiterals.size() <= element.id()) {
        inSetLiterals.resize(element.id() + 1);
    }
    std::optional<BooleanValue>& maybeBool = inSetLiterals[element.id()];
    if (!maybeBool.has_value()) {
        maybeBool = solver.impl().newBoolean(elementInSetTheory);
        inSetInfos[maybeBool.value()] = { .element = element, .set = literal.set() };
    }
    return literal.contained() ? maybeBool.value() : !maybeBool.value();
}

std::pair<Sets::ElementId, Sets::Containment> Sets::mapFromBool(BooleanValue lit) {
    VERIFY(lit.theory() == elementInSetTheory);
    auto [element, set] = inSetInfos[lit];
    return { element, lit.negated() ? !in(set) : in(set) };
}

}