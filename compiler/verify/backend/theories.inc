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

SET_THEORY(MemorySet, memorySetsBaseTheory)
THEORY(MemoryLocationSets, MemorySet)

SET_THEORY(InvariantSet, invariantSetsBaseTheory)
THEORY(InclusiveLocationInvariantSets, InvariantSet)
THEORY(ExclusiveLocationInvariantSets, InvariantSet)
THEORY(PathInvariantSets, InvariantSet)
THEORY(InvariantSingletonSets, InvariantSet)

THEORY(MemberLiterals, Member)
THEORY(CompositeMembers, Member)
THEORY(AuxMemberVariables, Member)

REASON(Always, EmptyReasonData, true, alwaysReason)
REASON(Decision, DecisionData, false, decisionReason)
REASON(Clause, LiteralOccurrence, true, clauses)

REASON(MemberEquality, EmptyReasonData, false, members)
REASON(MemberDisequality, EmptyReasonData, false, members)

REASON(MemberPrefixHit, PrefixHitData, false, memorySets)
REASON(MemoryDeclarationsShareElement, SharedElementReason, false, memorySets)

REASON(InvariantPrefixHit, PrefixHitData, false, invariantSets)
REASON(InvariantDeclarationsShareElement, SharedElementReason, false, invariantSets)
REASON(InvariantSingletonSetsShareElement, SharedElementReason, false, invariantSets)
REASON(InvariantSingletonToInclusive, SingletonToInclusiveReason, false, invariantSets)
REASON(InvariantSingletonConflict, SharedElementReason, false, invariantSets)

USE_KIND(Test, useTest)
USE_KIND(KeyWatchesTestKey, useTest)
USE_KIND(KeyWatchesTestWatch, useTest)
USE_KIND(MemorySetRepresentative, memorySets)
USE_KIND(MemorySetPendingContainment, memorySets)
USE_KIND(MemoryPrefixWord, memorySets)
USE_KIND(InvariantSetRepresentative, invariantSets)
USE_KIND(InvariantSetPendingContainment, invariantSets)
USE_KIND(InvariantPrefixWord, invariantSets)
USE_KIND(InvariantSingletonKeyUse, invariantSets)
USE_KIND(InvariantSingletonWatchUse, invariantSets)

#undef REASON
#undef USE_KIND
#undef THEORY
#undef PAIR_THEORY
#undef UNINTERPRETED_EQUALITY_THEORY
#undef SET_THEORY