#ifndef THEORY
#define THEORY(name, valueKind)
#endif

#ifndef PAIR_THEORY
#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair) THEORY(name, theoryValueKind)
#endif

THEORY(TrueFalse, Boolean)
THEORY(ClauseGlueVariables, Boolean)
THEORY(AuxBooleanVariables, Boolean)

PAIR_THEORY(BooleanEquality, Boolean, Boolean, 1)
PAIR_THEORY(UninterpretedConstantEquality, Boolean, UninterpretedConstant, 2)
PAIR_THEORY(MemberEquality, Boolean, Member, 2)

THEORY(AuxUninterpretedConstants, UninterpretedConstant)

THEORY(MemberLiterals, Member)
THEORY(CompositeMembers, Member)
THEORY(AuxMemberVariables, Member)

#undef THEORY
#undef PAIR_THEORY