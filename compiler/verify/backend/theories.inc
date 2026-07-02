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

THEORY(TrueFalse, Boolean)
THEORY(ClauseGlueVariables, Boolean)
THEORY(AuxBooleanVariables, Boolean)

PAIR_THEORY(BooleanEquality, Boolean, Boolean, 1)
UNINTERPRETED_EQUALITY_THEORY(UninterpretedConstant, uninterpConstantEquality)
PAIR_THEORY(MemberEquality, Boolean, Member, 2)
PAIR_THEORY(UninterpretedConstantSetEquality, Boolean, UninterpretedConstantSet, 2)

THEORY(UninterpretedConstantSetIsEmpty, Boolean)
THEORY(UninterpretedConstantElementInSet, Boolean)

THEORY(AuxUninterpretedConstants, UninterpretedConstant)

THEORY(UninterpretedConstantEmptySet, UninterpretedConstantSet)
THEORY(UninterpretedConstantSetExpressions, UninterpretedConstantSet)
THEORY(UninterpretedConstantSingletonSets, UninterpretedConstantSet)
THEORY(AuxUninterpretedConstantSets, UninterpretedConstantSet)

THEORY(MemberLiterals, Member)
THEORY(CompositeMembers, Member)
THEORY(AuxMemberVariables, Member)

REASON(Always, EmptyReasonData, true, alwaysReason)
REASON(Decision, DecisionData, false, decisionReason)
REASON(Clause, LiteralOccurrence, true, clauses)

REASON(MemberEquality, EmptyReasonData, false, members)
REASON(MemberDisequality, EmptyReasonData, false, members)

REASON(SetClauseDefToExpr, SetClauseDefData, true, setReasons)
REASON(SetClauseExprToDef, SetClauseDefData, true, setReasons)
REASON(SetClauseExhaustive, LiteralOccurrence, true, setReasons)
REASON(SetEqualityToElem, SetEqualityToElemData, false, setReasons)
REASON(SetEmptyToElem, SetEmptyToElemData, false, setReasons)

#undef REASON
#undef THEORY
#undef PAIR_THEORY
#undef UNINTERPRETED_EQUALITY_THEORY