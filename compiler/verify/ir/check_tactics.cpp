#include <verify/ir/Match.h>
#include <verify/ir/check.h>
#include <verify/language/Parser.h>

#include <gtest/gtest.h>

namespace verify::ir {

struct PatternTactics {
    PatternTactics() {
#define PATTERN_TACTIC(name, snake_case, patterns...)   \
    for (const char* source : { patterns }) \
        add(Tactic::name, source);
#include <verify/ir/tactics.inc>
    }

    std::span<const Pattern> operator[](Tactic tactic) const {
        return m_patterns[std::to_underlying(tactic)];
    }

private:
    void add(Tactic tactic, const char* source) {
        m_patterns[std::to_underlying(tactic)].emplace_back(source);
    }

    std::array<std::vector<Pattern>, std::to_underlying(Tactic::COUNT)> m_patterns;
};

bool checkPatternTactic(const Function& function, Bool prop, Tactic tactic) {
    static const PatternTactics tactics;
    for (const Pattern& pattern : tactics[tactic]) {
        if (matchClause(pattern, function, prop).has_value())
            return true;
    }
    return false;
}

bool checkPhiEnumerate(const Function& function, Bool prop) {
    if (prop.kind() != ExprKind::Or || prop.negated())
        return false;
    auto clause = function.view(function.getOr(prop).operands);
    VERIFY(clause.size() >= 2);

    Bool notActiveLit = (Bool)clause.back();
    if (notActiveLit.kind() != ExprKind::PositionActive || !notActiveLit.negated())
        return false;
    CodePos phiPos = notActiveLit.getPositionActive();
    // The position behind the last instruction can be talked about, but it holds no phi
    if (phiPos.id() >= function.here().id() || function.opcodeAt(phiPos) != Opcode::Phi)
        return false;

    ControlFlowEdgeList edges = function.incomingEdges(phiPos);
    int_t offset = (int_t)clause.size() - edges.size() - 1;
    if (offset < 0)
        return false;
    for (int_t edgeIndex = 0; edgeIndex < edges.size(); edgeIndex++) {
        Bool edgeTakenLit = (Bool)clause[edgeIndex + offset];
        if (edgeTakenLit.kind() != ExprKind::ControlFlowEdgeTaken || edgeTakenLit.negated())
            return false;
        if (edgeTakenLit.getControlFlowEdgeTaken() != edges.at(edgeIndex))
            return false;
    }
    return true;
}

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
    prove !@phi.from@entry or @entry.active by phi_active_source
    prove !@phi.from@entry or $x.load@phi = $x.load@entry by phi_load
)")
            .empty());

    // The two clauses of 'phi_activate' and 'phi_active_source' are not interchangeable
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@entry:
    jump @phi
@phi:
    phi @entry
    prove !@phi.from@entry or @entry.active by phi_activate
    prove !@phi.from@entry or @phi.active by phi_active_source
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

TEST(VerifyIR, PhiEnumerate) {
    EXPECT_TRUE(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@second:
    jump @phi
@phi:
    phi @first, @second
    prove @phi.from@first or @phi.from@second or !@phi.active by phi_enumerate
)")
            .empty());

    // A phi with a single edge leaves no choice about which one is taken
    EXPECT_TRUE(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@phi:
    phi @first
    prove @phi.from@first or !@phi.active by phi_enumerate
)")
            .empty());

    // All edges of the phi have to be enumerated
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@second:
    jump @phi
@phi:
    phi @first, @second
    prove @phi.from@first or !@phi.active by phi_enumerate
)"),
        theorems({ 0 }));

    // The edges are enumerated in the order the phi lists them in
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@second:
    jump @phi
@phi:
    phi @first, @second
    prove @phi.from@second or @phi.from@first or !@phi.active by phi_enumerate
)"),
        theorems({ 0 }));

    // The edges are stated positively and the activity of the phi is negated
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@phi:
    phi @first
    prove !@phi.from@first or !@phi.active by phi_enumerate
)"),
        theorems({ 0 }));
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@phi:
    phi @first
    prove @phi.from@first or @phi.active by phi_enumerate
)"),
        theorems({ 0 }));

    // A single edge is no clause, so nothing follows from it on its own
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@phi:
    phi @first
    prove @phi.from@first by phi_enumerate
)"),
        theorems({ 0 }));

    // The enumerated edges are the ones of the phi that is claimed to be inactive
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
    prove @phi.from@first or !@other.active by phi_enumerate
)"),
        theorems({ 0 }));

    // Only a phi is entered through edges, no other instruction is
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@phi:
    phi @first
    prove @phi.from@first or !@first.active by phi_enumerate
)"),
        theorems({ 0 }));

    // The position behind the last instruction holds no phi either
    EXPECT_EQ(invalidProofs(R"(
fn #test():
@first:
    jump @phi
@phi:
    phi @first
@end:
    prove @phi.from@first or !@end.active by phi_enumerate
)"),
        theorems({ 0 }));

    // The clause may be weakened by further operands in front of the enumeration
    EXPECT_TRUE(invalidProofs(R"(
fn #test($a):
@first:
    jump @phi
@phi:
    phi @first
    prove $a = $a or @phi.from@first or !@phi.active by phi_enumerate
)")
            .empty());

    // The enumeration still has to end the clause
    EXPECT_EQ(invalidProofs(R"(
fn #test($a):
@first:
    jump @phi
@phi:
    phi @first
    prove @phi.from@first or !@phi.active or $a = $a by phi_enumerate
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
