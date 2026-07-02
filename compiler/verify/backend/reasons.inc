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

REASON(SetClauseDefToExpr, SetClauseDefData, true, setReasons)
REASON(SetClauseExprToDef, SetClauseDefData, true, setReasons)
REASON(SetClauseExhaustive, LiteralOccurrence, true, setReasons)
REASON(SetEqualityToElem, SetEqualityToElemData, false, setReasons)
REASON(SetEmptyToElem, SetEmptyToElemData, false, setReasons)

#undef REASON