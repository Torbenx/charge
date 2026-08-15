#include <verify/ir/check.h>

#include <verify/ir/Match.h>
#include <verify/language/Parser.h>

#include <gtest/gtest.h>

namespace verify::ir {

//! The theorems of 'source' whose proof does not establish their proposition
static std::vector<Theorem> invalidProofs(const char* source) {
    Function function = language::parse(source).function;
    FunctionCheckReport report = check(function);
    // A malformed function would make the result of the proof check meaningless
    VERIFY(report.malformedExpressions.empty() && report.malformedInstructions.empty());
    return report.invalidProofs;
}

static std::vector<Theorem> theorems(std::initializer_list<uint32_t> ids) {
    std::vector<Theorem> result;
    for (uint32_t id : ids)
        result.emplace_back(id);
    return result;
}

TEST(VerifyIR, EqualityReflexive) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($a):
    prove $a = $a by eq_reflexive
)")
            .empty());

    // Both sides have to be the same expression
    EXPECT_EQ(invalidProofs(R"(
fn #test($a, $b):
    prove $a = $b by eq_reflexive
)"),
        theorems({ 0 }));

    // The negation of a proposition is not the proposition
    EXPECT_EQ(invalidProofs(R"(
fn #test($a):
    prove $a != $a by eq_reflexive
)"),
        theorems({ 0 }));
}

TEST(VerifyIR, LoadStore) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($x, $target, $value: bool):
    pre %scalar: $x.type.bool_scalar
    store $target <- $value
@after:
    prove $x != $target or $x.load@after = $value by load_store
)")
            .empty());

    // The clause is about the store right before the position that is loaded at
    EXPECT_EQ(invalidProofs(R"(
fn #test($x, $target, $value: bool, $other):
    pre %scalar: $x.type.bool_scalar
    store $target <- $value
    store $other <- $value
@after:
    prove $x != $target or $x.load@after = $value by load_store
)"),
        theorems({ 1 }));

    // The stored value has to be the one the load is claimed to see
    EXPECT_EQ(invalidProofs(R"(
fn #test($x, $target, $value: bool, $other: bool):
    pre %scalar: $x.type.bool_scalar
    store $target <- $other
@after:
    prove $x != $target or $x.load@after = $value by load_store
)"),
        theorems({ 1 }));

    // Loading before the store proves nothing about it
    EXPECT_EQ(invalidProofs(R"(
fn #test($x, $target, $value: bool):
    pre %scalar: $x.type.bool_scalar
@before:
    store $target <- $value
    prove $x != $target or $x.load@before = $value by load_store
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, LoadStoreOfAnySort) {
    // The sort of the load is a placeholder, so the one clause covers all of them
    EXPECT_TRUE(invalidProofs(R"(
fn #test($x, $target, $value: memory_decl):
    pre %scalar: $x.type.memory_decl_scalar
    store $target <- $value
@after:
    prove $x != $target or $x.load@after = $value by load_store
)")
            .empty());
}

TEST(VerifyIR, SkipStore) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($x, $target, $value: bool):
    pre %scalar: $x.type.bool_scalar
@before:
    store $target <- $value
@after:
    prove $x = $target or $x.load@after = $x.load@before by skip_store
)")
            .empty());

    // Only a single store may sit between the two positions
    EXPECT_EQ(invalidProofs(R"(
fn #test($x, $target, $value: bool):
    pre %scalar: $x.type.bool_scalar
@before:
    store $target <- $value
    store $target <- $value
@after:
    prove $x = $target or $x.load@after = $x.load@before by skip_store
)"),
        theorems({ 1 }));

    // The location that is skipped has to be the one that is stored to
    EXPECT_EQ(invalidProofs(R"(
fn #test($x, $target, $value: bool, $other):
    pre %scalar: $x.type.bool_scalar
@before:
    store $other <- $value
@after:
    prove $x = $target or $x.load@after = $x.load@before by skip_store
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, PhiTactics) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($x):
    pre %scalar: $x.type.bool_scalar
@entry:
    jump @phi
@phi:
    phi @entry
    prove !@phi.from@entry or @phi.active by phi_activate
    prove !@phi.from@entry or @entry.active by phi_active_backward
    prove !@phi.from@entry or $x.load@phi = $x.load@entry by phi_load
)")
            .empty());

    // The two clauses of 'phi_activate' and 'phi_active_backward' are not interchangeable
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@entry:
    jump @phi
@phi:
    phi @entry
    prove !@phi.from@entry or @entry.active by phi_activate
    prove !@phi.from@entry or @phi.active by phi_active_backward
)"),
        theorems({ 0, 1 }));

    // 'phi_load' is about the two ends of the edge, not about any other position
    EXPECT_EQ(invalidProofs(R"(
fn #test($x):
    pre %scalar: $x.type.bool_scalar
@entry:
    jump @phi
@phi:
    phi @entry
@after:
    prove !@phi.from@entry or $x.load@after = $x.load@entry by phi_load
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, PhiExclusivity) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@second:
    jump @phi
@phi:
    phi @first, @second
    prove !@phi.from@first or !@phi.from@second by phi_exclusivity
)")
            .empty());

    // One edge is not exclusive with itself
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@second:
    jump @phi
@phi:
    phi @first, @second
    prove !@phi.from@first or !@phi.from@first by phi_exclusivity
)"),
        theorems({ 0 }));

    // Edges of two different phis say nothing about each other
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@second:
    jump @other
@phi:
    phi @first
@other:
    phi @second
    prove !@phi.from@first or !@other.from@second by phi_exclusivity
)"),
        theorems({ 0 }));
}

TEST(VerifyIR, JumpActiveForward) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test():
@jump:
    jump @phi
@phi:
    phi @jump
    prove !@jump.active or @phi.from@jump by jump_active_forward
)")
            .empty());

    // The jump has to lead to the phi the edge belongs to
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@jump:
    jump @other
@other:
    nop
@phi:
    phi @jump
    prove !@jump.active or @phi.from@jump by jump_active_forward
)"),
        theorems({ 0 }));

    // The position the edge comes from has to be the jump
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@jump:
    jump @phi
@other:
    jump @phi
@phi:
    phi @jump, @other
    prove !@jump.active or @phi.from@other by jump_active_forward
)"),
        theorems({ 0 }));
}

TEST(VerifyIR, BranchTactics) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($cond: bool):
@branch:
    branch $cond, @if_true, @if_false
@if_true:
    phi @branch
@if_false:
    phi @branch
    prove !@branch.active or !$cond or @if_true.from@branch by branch_active_forward
    prove !@branch.active or $cond or @if_false.from@branch by branch_active_forward
    prove !@if_true.from@branch or $cond by branch_decision
    prove !@if_false.from@branch or !$cond by branch_decision
)")
            .empty());

    // Taking an edge decides the condition, and it decides it the other way for the other edge
    EXPECT_EQ(invalidProofs(R"(
fn #test($cond: bool):
@branch:
    branch $cond, @if_true, @if_false
@if_true:
    phi @branch
@if_false:
    phi @branch
    prove !@if_true.from@branch or !$cond by branch_decision
    prove !@if_false.from@branch or $cond by branch_decision
)"),
        theorems({ 0, 1 }));

    // The condition of the clause has to be the one the branch is on
    EXPECT_EQ(invalidProofs(R"(
fn #test($cond: bool, $other: bool):
@branch:
    branch $cond, @if_true, @if_false
@if_true:
    phi @branch
@if_false:
    phi @branch
    prove !@if_true.from@branch or $other by branch_decision
)"),
        theorems({ 0 }));
}

TEST(VerifyIR, WeakenedClause) {
    // A clause stays true when it is weakened, so further operands in front are accepted
    EXPECT_TRUE(invalidProofs(R"(
fn #test($a):
@entry:
    jump @phi
@phi:
    phi @entry
    prove $a = $a or !@phi.from@entry or @phi.active by phi_activate
)")
            .empty());

    // The clause still has to end in the shape of the tactic
    EXPECT_EQ(invalidProofs(R"(
fn #test($a):
@entry:
    jump @phi
@phi:
    phi @entry
    prove !@phi.from@entry or @phi.active or $a = $a by phi_activate
)"),
        theorems({ 0 }));

    // The operands keep the order the tactic states them in
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@entry:
    jump @phi
@phi:
    phi @entry
    prove @phi.active or !@phi.from@entry by phi_activate
)"),
        theorems({ 0 }));
}

TEST(VerifyIR, UnprovenTheorems) {
    Function function = language::parse(R"(
fn #test($a, $b):
    pre %assumed: $a = $b
    prove $a = $a by eq_reflexive
    prove $b = $b by sorry
)")
                            .function;
    FunctionCheckReport report = check(function);

    // A precondition is assumed and a proven theorem is checked, everything else is left open
    EXPECT_TRUE(report.invalidProofs.empty());
    EXPECT_EQ(report.sorryTheorems, theorems({ 2 }));
    EXPECT_FALSE(report.ok());
}

}
