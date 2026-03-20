#ifndef REASON
#define REASON(name, data, fullyPropagating)
#endif

REASON(Always, EmptyReasonData, true)
REASON(Decision, DecisionData, false)
REASON(Clause, LiteralInstance, true)

#undef REASON