#ifndef REASON
#define REASON(name, data, fullyPropagating, implMember)
#endif

REASON(Always, EmptyReasonData, true, alwaysReason)
REASON(Decision, DecisionData, false, decisionReason)
REASON(Clause, LiteralOccurrence, true, clauses)

REASON(Equality, EmptyReasonData, true, rewriteEqualities)
REASON(Disequality, DisequalityReason, true, rewriteEqualities)
REASON(DisequalityByAlwaysDisequal, DisequalityReason, true, rewriteEqualities)

REASON(MemberEquality, EmptyReasonData, false, members)
REASON(MemberDisequality, EmptyReasonData, false, members)

REASON(SetClauseDefToExpr, SetClauseDefData, true, sets)
REASON(SetClauseExprToDef, SetClauseDefData, true, sets)
REASON(SetClauseExhaustive, LiteralOccurrence, true, sets)
REASON(SetEqualityToElem, SetEqualityToElemData, false, sets)

#undef REASON