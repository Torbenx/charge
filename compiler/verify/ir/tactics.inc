#ifndef TACTIC
#define TACTIC(name)
#endif

#ifndef SIMPLE_TACTIC
#define SIMPLE_TACTIC(name) TACTIC(name)
#endif

#ifndef COMPLEX_TACTIC
#define COMPLEX_TACTIC(name, type) TACTIC(name)
#endif

// Temporary placeholder for a missing proof
SIMPLE_TACTIC(Sorry)

// Proves generic propositions by running an SMT style solver against them
COMPLEX_TACTIC(SmtSolve, SmtParameters)

TACTIC(Precondition)

// Proves generic propositions P by showing that !P is incompatible with a set of proven clauses
COMPLEX_TACTIC(Sat, SatProof)

// Proves propositions of the form x = x
SIMPLE_TACTIC(EqualityReflexive)

// Proves propositions of the form x_1 != x_2 or x_2 != x_3 or ... or x_(n-1) != x_n or x_1 = x_n
SIMPLE_TACTIC(EqualityTransitive)

// Proves the disjunction of linear integer formulars by means of Farkas lemma
COMPLEX_TACTIC(IntFarkas, IntFarkasProof)

// For a store store_target <- store_value proves x != store_target or x.load@after_store = store_value
SIMPLE_TACTIC(LoadStore)

// For a store to store_target proves x = store_target or x.load@after_store = x.load@before_store
SIMPLE_TACTIC(SkipStore)

// Proves that for an active phi one of its edges is taken
// For @phi: phi @a @b @c this proves !@phi.active or @phi.from@a or @phi.from@b or @phi.from@c
SIMPLE_TACTIC(PhiEnumerate)

// Proves that phi parents are mutually exlusive
// !@phi.from@a or !@phi.from@b for all pairs of parents @a, @b
SIMPLE_TACTIC(PhiExclusivity)

// !@phi.from@a or @phi.active for all parents @a
SIMPLE_TACTIC(PhiActivate)

// !@phi.from@label or @label.active
SIMPLE_TACTIC(PhiActiveBackward)

// !@phi.from@label or x.load@phi = x.load@label
SIMPLE_TACTIC(PhiLoad)

// For @jump: jump @phi proves !@jump.active or @phi.from@jump
SIMPLE_TACTIC(JumpActiveForward)

// For @branch: branch cond, @if_true, @if_false
// !@branch.active or !cond or @if_true.from@branch
// !@branch.active or cond or @if_false.from@branch
SIMPLE_TACTIC(BranchActiveForward)

// For @branch: branch cond, @if_true, @if_false
// !@if_true.from@branch or cond
// !@if_false.from@branch or !cond
SIMPLE_TACTIC(BranchDecision)

#undef TACTIC
#undef SIMPLE_TACTIC
#undef COMPLEX_TACTIC