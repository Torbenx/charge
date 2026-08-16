#ifndef TACTIC
#define TACTIC(name, snake_case)
#endif

#ifndef SIMPLE_TACTIC
#define SIMPLE_TACTIC(name, snake_case) TACTIC(name, snake_case)
#endif

#ifndef COMPLEX_TACTIC
#define COMPLEX_TACTIC(name, snake_case, type) TACTIC(name, snake_case)
#endif

#ifndef PATTERN_TACTIC
#define PATTERN_TACTIC(name, snake_case, patterns...) SIMPLE_TACTIC(name, snake_case)
#endif

// Temporary placeholder for a missing proof
SIMPLE_TACTIC(Sorry, sorry)

// Proves generic propositions by running an SMT style solver against them
COMPLEX_TACTIC(SmtSolve, smt_solve, SmtParameters)

TACTIC(Precondition, precondition)

// Proves generic propositions P by showing that !P is incompatible with a set of proven clauses
COMPLEX_TACTIC(Sat, sat, SatProof)

// Proves propositions of the form x_1 != x_2 or x_2 != x_3 or ... or x_(n-1) != x_n or x_1 = x_n
SIMPLE_TACTIC(EqualityTransitive, eq_transitive)

// Proves the disjunction of linear integer formulars by means of Farkas lemma
COMPLEX_TACTIC(IntFarkas, int_farkas, IntFarkasProof)

// Proves that for an active phi one of its edges is taken
// For @phi: phi @a @b @c this proves @phi.from@a or @phi.from@b or @phi.from@c or !@phi.active
SIMPLE_TACTIC(PhiEnumerate, phi_enumerate)

PATTERN_TACTIC(EqualityReflexive, eq_reflexive, R"(
fn #_($x: uninterpreted_constant):

    prove $x = $x by sorry
)")

PATTERN_TACTIC(LoadStore, load_store, R"(
fn #_($x, $target, $value: uninterpreted_constant):
    pre %scalar: $x.type.uninterpreted_constant_scalar

@store:
    store $target <- $value
@after:
    nop

    prove $x != $target or $x.load@after = $value by sorry
)")

PATTERN_TACTIC(SkipStore, skip_store, R"(
fn #_($x, $target, $value: uninterpreted_constant):
    pre %scalar: $x.type.uninterpreted_constant_scalar

@before:
    store $target <- $value
@after:
    nop

    prove $x = $target or $x.load@after = $x.load@before by sorry
)")

// Proves that the incoming control flow edges of a phi are mutually exlusive
PATTERN_TACTIC(PhiExclusivity, phi_exclusivity, R"(
fn #_():
@first:
    nop
@second:
    nop

@phi:
    phi @first, @second

    prove !@phi.from@first or !@phi.from@second by sorry
)")

PATTERN_TACTIC(PhiActivate, phi_activate, R"(
fn #_():
@source:
    nop

@phi:
    phi @source

    prove !@phi.from@source or @phi.active by sorry
)")

PATTERN_TACTIC(PhiActiveSource, phi_active_source, R"(
fn #_():
@source:
    nop

@phi:
    phi @source

    prove !@phi.from@source or @source.active by sorry
)")

PATTERN_TACTIC(PhiLoad, phi_load, R"(
fn #_($x):
    pre %scalar: $x.type.uninterpreted_constant_scalar
@source:
    nop

@phi:
    phi @source

    prove !@phi.from@source or $x.load@phi = $x.load@source by sorry
)")

PATTERN_TACTIC(BranchDecision, branch_decision, R"(
fn #_($cond: bool):
@if_true:
    phi @branch
    nop
@if_false:
    phi @branch
    nop
@branch:
    branch $cond, @if_true, @if_false

    prove !@if_true.from@branch or $cond by sorry
)",
    R"(
fn #_($cond: bool):
@if_true:
    phi @branch
    nop
@if_false:
    phi @branch
    nop
@branch:
    branch $cond, @if_true, @if_false

    prove !@if_false.from@branch or !$cond by sorry
)")

#undef TACTIC
#undef SIMPLE_TACTIC
#undef COMPLEX_TACTIC
#undef PATTERN_TACTIC