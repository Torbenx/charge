#ifndef REASON
#define REASON(name, data, fullyPropagating)
#endif

REASON(Decision, DecisionData, false)
REASON(Clause, LiteralInstance, true)

#undef REASON