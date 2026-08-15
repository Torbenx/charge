#include <verify/ir/check.h>

#include <verify/language/Parser.h>

#include <gtest/gtest.h>

namespace verify::ir {

//! The result of checking the function 'source' describes
static FunctionCheckReport checkSource(const char* source) {
    Function function = language::parse(source).function;
    FunctionCheckReport report = check(function);
    // A malformed function would make the result of the proof check meaningless
    VERIFY(report.malformedExpressions.empty() && report.malformedInstructions.empty());
    return report;
}

//! The theorems of 'source' whose proof does not establish their proposition
static std::vector<Theorem> invalidProofs(const char* source) {
    return checkSource(source).invalidProofs;
}

static std::vector<Theorem> theorems(std::initializer_list<uint32_t> ids) {
    std::vector<Theorem> result;
    for (uint32_t id : ids)
        result.emplace_back(id);
    return result;
}

TEST(VerifyIR, SatResolution) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause !$p or $q by sorry
        clause $p by sorry
)")
            .empty());

    // Without the second clause nothing rules out that both of them are false
    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause !$p or $q by sorry
)"),
        theorems({ 1 }));

    // Clauses that say nothing about the proposition do not establish it either
    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause $p by sorry
        clause !$p or $p by sorry
)"),
        theorems({ 2 }));
}

TEST(VerifyIR, SatClausesAreTheorems) {
    // Writing a clause down states a theorem, so its own proof is checked next to the sat
    // proof that rests on it and not as part of it
    FunctionCheckReport report = checkSource(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause !$p or $q by eq_reflexive
        clause $p by sorry
)");
    EXPECT_EQ(report.invalidProofs, theorems({ 0 }));
    EXPECT_EQ(report.sorryTheorems, theorems({ 1 }));
}

TEST(VerifyIR, SatClausesPrecedeTheirTheorem) {
    // 'p or p' leaves no way for 'p' to be false, so the clause does establish the proposition
    {
        Function fn;
        Bool p(fn.addParameter(Sort::Bool));
        Theorem clause = fn.addTheorem(fn.addOr({ p, p }), CodePos(0));
        fn.addTheorem(p, CodePos(0), fn.addSat({ { clause } }));
        EXPECT_TRUE(check(fn).invalidProofs.empty());
    }

    // The same proof is rejected when the clause is only stated after the theorem it serves.
    // The text form cannot express this, but a proof resting on itself would look like it.
    {
        Function fn;
        Bool p(fn.addParameter(Sort::Bool));
        Theorem theorem = fn.addTheorem(p, CodePos(0), fn.addSat({ { Theorem(1) } }));
        EXPECT_EQ(fn.addTheorem(fn.addOr({ p, p }), CodePos(0)), Theorem(1));
        EXPECT_EQ(check(fn).invalidProofs, std::vector<Theorem> { theorem });
    }
}

TEST(VerifyIR, SatClauseOfStatedTheorem) {
    // A clause may name a theorem that was stated elsewhere instead of stating a new one
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove %p_holds: $p by sorry
    prove $q by sat:
        clause !$p or $q by sorry
        clause %p_holds
)")
            .empty());

    // The same theorem may serve as a clause of more than one proof
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool, $r: bool):
    prove %p_holds: $p by sorry
    prove $q by sat:
        clause !$p or $q by sorry
        clause %p_holds
    prove $r by sat:
        clause !$p or $r by sorry
        clause %p_holds
)")
            .empty());
}

TEST(VerifyIR, SatTautology) {
    // A proposition that cannot be false at all is established by any set of clauses
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $p or !$p by sat:
        clause $q by sorry
)")
            .empty());

    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $p or $p by sat:
        clause $q by sorry
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, SatConnectives) {
    // The negated proposition is a disjunction, so both operands have to be forced
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $p and $q by sat:
        clause $p by sorry
        clause $q by sorry
)")
            .empty());

    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $p and $q by sat:
        clause $p by sorry
)"),
        theorems({ 1 }));

    // A negated 'and' clause states the disjunction of the negated operands
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove !$q by sat:
        clause !($p and $q) by sorry
        clause $p by sorry
)")
            .empty());

    // A conjunction below a disjunction is not a clause of its own and needs a variable
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool, $q: bool, $r: bool):
    prove $r by sat:
        clause ($p and $q) or $r by sorry
        clause !$p or !$q by sorry
)")
            .empty());

    // Without the second clause the conjunction can be true and 'r' false
    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool, $r: bool):
    prove $r by sat:
        clause ($p and $q) or $r by sorry
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, SatRepeatedOperands) {
    // Operands standing for the same literal state it once, they are not a clause of two
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool):
    prove $p by sat:
        clause $p or $p by sorry
)")
            .empty());

    // A clause holding a literal next to its complement holds always and states nothing
    EXPECT_EQ(invalidProofs(R"(
fn #test($p: bool, $q: bool):
    prove $q by sat:
        clause $p or !$p by sorry
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, SatBooleanLiterals) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test($q: bool):
    prove true by sat:
        clause $q by sorry
)")
            .empty());

    // Everything follows from a false clause
    EXPECT_TRUE(invalidProofs(R"(
fn #test($p: bool):
    prove $p by sat:
        clause false by sorry
)")
            .empty());

    EXPECT_EQ(invalidProofs(R"(
fn #test():
    prove false by sat:
        clause true by sorry
)"),
        theorems({ 1 }));
}

TEST(VerifyIR, SatVariables) {
    // Propositions that are not connectives are opaque, so only the same expression resolves
    EXPECT_TRUE(invalidProofs(R"(
fn #test($a, $b, $c):
    prove $a = $c by sat:
        clause $a = $b or $a = $c by sorry
        clause $a != $b by sorry
)")
            .empty());

    // The order of the operands is part of the identity of an equality
    EXPECT_EQ(invalidProofs(R"(
fn #test($a, $b, $c):
    prove $a = $c by sat:
        clause $a = $b or $a = $c by sorry
        clause $b != $a by sorry
)"),
        theorems({ 2 }));
}

TEST(VerifyIR, SatProofOfPhiActivity) {
    // The clauses of the phi tactics combine to the activity of a phi that was jumped to
    EXPECT_TRUE(invalidProofs(R"(
fn #test():
@jump:
    jump @phi
    nop
@phi:
    phi @jump
    nop
    prove !@jump.active or @phi.active by sat:
        clause !@jump.active or @phi.from@jump by jump_active_forward
        clause !@phi.from@jump or @phi.active by phi_activate
)")
            .empty());
}

}
