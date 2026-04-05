#ifndef REASON
#define REASON(name, data, fullyPropagating, implMember)
#endif

REASON(Always, EmptyReasonData, true, alwaysReason)
REASON(Decision, DecisionData, false, decisionReason)
REASON(Clause, LiteralInstance, true, clauses)
REASON(Equality, EmptyReasonData, true, rewriteEqualities)
REASON(Disequality, DisequalityReason, true, rewriteEqualities)
REASON(DisequalityByAlwaysDisequal, DisequalityReason, true, rewriteEqualities)
REASON(MemberEquality, EmptyReasonData, false, members)
REASON(MemberDisequality, EmptyReasonData, false, members)

#undef REASON