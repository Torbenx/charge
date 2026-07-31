#ifndef THEORY
#define THEORY(name, sort)
#endif

#ifndef REASON
#define REASON(name, data, fullyPropagating, implMember)
#endif

#ifndef USE_KIND
#define USE_KIND(name, implMember)
#endif

#ifndef PAIR_THEORY
#define PAIR_THEORY(name, theorySort, pairSort, valuesPerPair) THEORY(name, theorySort)
#endif

#ifndef UNINTERPRETED_EQUALITY_THEORY
#define UNINTERPRETED_EQUALITY_THEORY(sort, memberName)                \
    PAIR_THEORY(sort##Equality, Boolean, sort, 2)                      \
    REASON(sort##Equality, EmptyReasonData, true, memberName)          \
    REASON(sort##Disequality, DisequalityReason, true, memberName)     \
    REASON(sort##DisequalityByAlwaysDisequal, DisequalityReason, true, memberName)
#endif

// Important: For Sets::refineClause() to work correctly it needs to see all the clauses.
// So all reasons involving ElementInSet literals need to marked as not fully propagating
// otherwise they maybe elided from clause learning.
#ifndef SET_THEORY
#define SET_THEORY(sort, memberName)                                          \
    PAIR_THEORY(sort##Equality, Boolean, sort, 2)                             \
    THEORY(sort##ElementInSet, Boolean)                                       \
    THEORY(sort##EmptySet, sort)                                              \
    THEORY(sort##Expressions, sort)                                           \
    REASON(sort##ClauseDefToExpr, SetClauseDefData, false, memberName)        \
    REASON(sort##ClauseExprToDef, SetClauseDefData, false, memberName)        \
    REASON(sort##ClauseExhaustive, LiteralOccurrence, false, memberName)      \
    REASON(sort##EqualityToElement, SetEqualityToElemData, false, memberName) \
    REASON(sort##ForAllDistribute, EmptyReasonData, false, memberName)        \
    REASON(sort##ForAllWitness, SetForAllWitnessData, false, memberName)
#endif

THEORY(TrueFalse, Boolean)
THEORY(ClauseGlueVariables, Boolean)
THEORY(AuxBooleanVariables, Boolean)

PAIR_THEORY(BooleanEquality, Boolean, Boolean, 1)
UNINTERPRETED_EQUALITY_THEORY(UninterpretedConstant, uninterpConstantEquality)
PAIR_THEORY(MemberEquality, Boolean, Member, 2)
UNINTERPRETED_EQUALITY_THEORY(MemoryDeclaration, memoryDeclarationEquality)

THEORY(AuxUninterpretedConstants, UninterpretedConstant)

SET_THEORY(UninterpretedConstantSet, uninterpConstantSets)
THEORY(AuxUninterpretedConstantSets, UninterpretedConstantSet)
THEORY(UninterpretedConstantSingletonSets, UninterpretedConstantSet)
REASON(UninterpretedConstantInSingleton, InSingletonReason, false, uninterpConstantSingletons)

THEORY(AuxMemoryDeclarationVariables, MemoryDeclaration)

SET_THEORY(MemoryLocationSet, memorySets)
THEORY(MemoryLocationSets, MemoryLocationSet)

THEORY(MemberLiterals, Member)
THEORY(CompositeMembers, Member)
THEORY(AuxMemberVariables, Member)

REASON(Always, EmptyReasonData, true, alwaysReason)
REASON(Decision, DecisionData, false, decisionReason)
REASON(Clause, LiteralOccurrence, true, clauses)

REASON(MemberEquality, EmptyReasonData, false, members)
REASON(MemberDisequality, EmptyReasonData, false, members)

REASON(MemberPrefixHit, PrefixHitData, false, memoryLocationSets)
REASON(MemoryDeclarationsShareElement, SharedElementReason, false, memoryLocationSets)

USE_KIND(Test, useTest)
USE_KIND(MemoryLocationSetRepresentative, memoryLocationSets)
USE_KIND(MemoryLocationSetPendingContainment, memoryLocationSets)
USE_KIND(MemberPrefixWord, memoryLocationSets)

#undef REASON
#undef USE_KIND
#undef THEORY
#undef PAIR_THEORY
#undef UNINTERPRETED_EQUALITY_THEORY
#undef SET_THEORY