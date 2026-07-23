#ifndef THEORY
#define THEORY(name, valueKind)
#endif

#ifndef REASON
#define REASON(name, data, fullyPropagating, implMember)
#endif

#ifndef PAIR_THEORY
#define PAIR_THEORY(name, theoryValueKind, pairValueKind, valuesPerPair) THEORY(name, theoryValueKind)
#endif

#ifndef UNINTERPRETED_EQUALITY_THEORY
#define UNINTERPRETED_EQUALITY_THEORY(valueKind, memberName)            \
    PAIR_THEORY(valueKind##Equality, Boolean, valueKind, 2)             \
    REASON(valueKind##Equality, EmptyReasonData, true, memberName)      \
    REASON(valueKind##Disequality, DisequalityReason, true, memberName) \
    REASON(valueKind##DisequalityByAlwaysDisequal, DisequalityReason, true, memberName)
#endif

#ifndef SET_THEORY
#define SET_THEORY(valueKind, memberName)                                          \
    PAIR_THEORY(valueKind##Equality, Boolean, valueKind, 2)                        \
    THEORY(valueKind##ElementInSet, Boolean)                                       \
    THEORY(valueKind##EmptySet, valueKind)                                         \
    THEORY(valueKind##Expressions, valueKind)                                      \
    REASON(valueKind##ClauseDefToExpr, SetClauseDefData, true, memberName)         \
    REASON(valueKind##ClauseExprToDef, SetClauseDefData, true, memberName)         \
    REASON(valueKind##ClauseExhaustive, LiteralOccurrence, true, memberName)       \
    REASON(valueKind##EqualityToElement, SetEqualityToElemData, false, memberName) \
    REASON(valueKind##ForAllDistribute, EmptyReasonData, false, memberName)
#endif

THEORY(TrueFalse, Boolean)
THEORY(ClauseGlueVariables, Boolean)
THEORY(AuxBooleanVariables, Boolean)

PAIR_THEORY(BooleanEquality, Boolean, Boolean, 1)
UNINTERPRETED_EQUALITY_THEORY(UninterpretedConstant, uninterpConstantEquality)
PAIR_THEORY(MemberEquality, Boolean, Member, 2)

THEORY(AuxUninterpretedConstants, UninterpretedConstant)

SET_THEORY(UninterpretedConstantSet, uninterpConstantSets)
THEORY(AuxUninterpretedConstantSets, UninterpretedConstantSet)
THEORY(UninterpretedConstantSingletonSets, UninterpretedConstantSet)
REASON(UninterpretedConstantInSingleton, InSingletonReason, false, uninterpConstantSingletons)

THEORY(MemberLiterals, Member)
THEORY(CompositeMembers, Member)
THEORY(AuxMemberVariables, Member)

REASON(Always, EmptyReasonData, true, alwaysReason)
REASON(Decision, DecisionData, false, decisionReason)
REASON(Clause, LiteralOccurrence, true, clauses)

REASON(MemberEquality, EmptyReasonData, false, members)
REASON(MemberDisequality, EmptyReasonData, false, members)

#undef REASON
#undef THEORY
#undef PAIR_THEORY
#undef UNINTERPRETED_EQUALITY_THEORY
#undef SET_THEORY