#include <verify/backend/Sets.h>

#include <verify/backend/SolverImpl.h>

#include <ranges>

namespace verify::backend {

Sets::Sets(Solver& solver, TheoryId expressionTheory, TheoryId equalityTheory, TheoryId isEmptyTheory, TheoryId elementInSetTheory)
    : setKind(kindOf(expressionTheory))
    , expressionTheory(expressionTheory)
    , equalityTheory(equalityTheory)
    , isEmptyTheory(isEmptyTheory)
    , elementInSetTheory(elementInSetTheory)
    , setInfos(solver, setKind)
    , inSetInfos(solver, elementInSetTheory) {
    // Construct empty set as union with no elements
    Value empty = Value(expressionTheory, clauses.size());
    clauses.push_back({ !in(empty) });
    VERIFY(empty == emptySet());
}

void Sets::newPair(Solver& solver, PairHandle pair) {
    VERIFY(!pair.specialPair());
    VERIFY(pair.valueKind() == setKind);
    auto [a, b] = solver.at(pair);
    solver.addClause({ makeEquality(pair), !makeIsEmpty(subset(solver, { a }, { b })), !makeIsEmpty(subset(solver, { b }, { a })) });
}

bool Sets::unionExpression(Value value) const {
    return value.theory() == expressionTheory && !clauses[value.id()].front().contained();
}

bool Sets::subsetExpression(Value value) const {
    return value.theory() == expressionTheory && clauses[value.id()].front().contained();
}

Value Sets::emptySet() { return Value(expressionTheory, 0); }

Value Sets::union_(Solver&, std::span<const Value> sets) {
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
        if (unionExpression(set)) {
            const auto& unionClause = clauses[set.id()];
            for (Containment lit : std::views::drop(unionClause, 1)) {
                VERIFY(!unionExpression(lit.set()));
                addSet(lit.set());
            }
        } else {
            addSet(set);
        }
    }
    if (clause.size() == 1) {
        return emptySet();
    } else if (clause.size() == 1) {
        return clause[1].set();
    } else {
        addClause(std::move(clause));
        return result;
    }
}

Value Sets::subset(Solver&, std::span<const Value> intersection, std::span<const Value> minus) {
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
        if (set == emptySet()) {
            return emptySet();
        }
        VERIFY(!unionExpression(set));
        if (subsetExpression(set)) {
            auto& subsetClause = clauses[set.id()];
            for (Containment lit : std::views::drop(subsetClause, 1)) {
                if (!addSet(lit))
                    return emptySet();
            }
        } else {
            if (!addSet(!in(set)))
                return emptySet();
        }
    }
    for (Value set : minus) {
        VERIFY(!subsetExpression(set));
        if (unionExpression(set)) {
            auto& unionClause = clauses[set.id()];
            for (Containment lit : std::views::drop(unionClause, 1)) {
                Value unionPart = lit.set();
                VERIFY(!subsetExpression(unionPart));
                VERIFY(!unionExpression(unionPart));
                if (!addSet(in(unionPart)))
                    return emptySet();
            }
        } else {
            if (!addSet(in(set)))
                return emptySet();
        }
    }
    VERIFY(clause.size() >= 2);
    if (clause.size() == 2) {
        VERIFY(!clause[1].contained()); // !contained indicates intersection
        return clause[1].set();
    } else {
        addClause(std::move(clause));
        return result;
    }
}

void Sets::addClause(std::vector<Containment> clause) {
    VERIFY(clause.size() <= MAX_CLAUSE_SIZE);
    uint32_t clauseId = clauses.size();
    VERIFY(clause.front().set() == Value(expressionTheory, clauseId));
    for (uint32_t i = 0; i < clause.size(); i++) {
        infoFor(clause[i]).occurrences.push_back({ .literalIndex = i, .clauseIndex = clauseId });
    }
    clauses.emplace_back(std::move(clause));
}

void Sets::propagateContainment(Solver& solver, ElementId element, Containment literal) {
    for (auto occ : infoFor(literal).occurrences) {
        const auto& clause = clauses[occ.clauseIndex];
        if (occ.literalIndex == 0) {
            // If the first literal in a clause is true than all other literals are false.
            for (Containment lit : std::views::drop(clause, 1))
                assignTrue(solver, element, lit, makeReason<ReasonKind::SetClauseDefToExpr>({ .def = !literal, .expr = lit }));
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

void Sets::unapplyContainment(Solver& solver, ElementId element, Containment literal) {
    // See also Clauses::unapplyAssignment()
    auto& clauseMasks = elements[element.id()].clauseMasks;
    for (auto occ : infoFor(!literal).occurrences) {
        auto& clauseMask = clauseMasks[occ.clauseIndex];
        auto mask = Clauses::literalMask(occ.literalIndex);
        clauseMask |= mask;
    }
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
        result.add(solver, map(solver, element, reason.get<ReasonKind::SetClauseDefToExpr>().def));
        return { solver.viewClause(result), 0 };
    case ReasonKind::SetClauseExprToDef:
        result.add(solver, boolLiteral);
        result.add(solver, map(solver, element, reason.get<ReasonKind::SetClauseDefToExpr>().expr));
        return { solver.viewClause(result), 0 };
    case ReasonKind::SetClauseExhaustive: {
        auto occ = reason.get<ReasonKind::SetClauseExhaustive>();
        for (Containment lit : clauses[occ.clauseIndex]) {
            result.add(solver, map(solver, element, lit));
        }
        return { solver.viewClause(result), occ.literalIndex };
    }
    case ReasonKind::SetEqualityToElem: {
        auto [pair, assignSource] = reason.get<ReasonKind::SetEqualityToElem>();
        result.add(solver, boolLiteral);
        result.add(solver, map(solver, element, !assignSource));
        result.add(solver, !makeEquality(pair));
        return { solver.viewClause(result), 0 };
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

BooleanValue Sets::map(Solver& solver, ElementId element, Containment literal) {
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

}