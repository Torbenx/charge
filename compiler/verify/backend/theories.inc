#ifndef THEORY
#define THEORY(name, valueKind)
#endif

#ifndef PAIR_THEORY
#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair) THEORY(name, theoryValueKind)
#endif

#ifndef EQUALITY_THEORY
#define EQUALITY_THEORY(valueKind) PAIR_THEORY(valueKind##Equality, Boolean, valueKind, 2)
#endif

THEORY(TrueFalse, Boolean)
THEORY(ClauseGlueVariables, Boolean)
THEORY(AuxBooleanVariables, Boolean)
THEORY(AuxUninterpretedConstants, UninterpretedConstant)
EQUALITY_THEORY(Boolean)
EQUALITY_THEORY(UninterpretedConstant)

#undef THEORY
#undef PAIR_THEORY
#undef EQUALITY_THEORY