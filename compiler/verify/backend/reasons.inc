#ifndef REASON
#define REASON(name, data, fullyPropagating, implMember)
#endif

REASON(Always, EmptyReasonData, true, alwaysReason)
REASON(Decision, DecisionData, false, decisionReason)
REASON(Clause, LiteralInstance, true, clauses)

#undef REASON