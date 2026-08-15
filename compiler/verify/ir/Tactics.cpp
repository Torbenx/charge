#include <verify/ir/Tactics.h>

#include <verify/ir/Match.h>

namespace verify::ir {

//! The clauses of every tactic that proves a fixed shape
/*!
Each of them is the statement documented above the tactic in 'tactics.inc' written in the text
form of the IR. A tactic that proves more than one shape lists them in the order of that
documentation and a proposition has to match one of them.

The instructions a pattern names are part of what it requires, the 'nop's between them are not:
they only keep the labels around them from being tied to each other.
*/
struct SimpleTactics {
    SimpleTactics();

    std::span<const Pattern> operator[](Tactic tactic) const {
        return m_patterns[std::to_underlying(tactic)];
    }

private:
    void add(Tactic tactic, const char* source) {
        m_patterns[std::to_underlying(tactic)].emplace_back(source);
    }

    std::array<std::vector<Pattern>, std::to_underlying(Tactic::COUNT)> m_patterns;
};

SimpleTactics::SimpleTactics() {
    add(Tactic::EqualityReflexive, R"(
fn #_($x):
    prove $x = $x by sorry
)");

    add(Tactic::LoadStore, R"(
fn #_($x, $target, $value):
    pre %scalar: $x.type.uninterpreted_constant_scalar
@store:
    store $target <- $value
@after:
    prove $x != $target or $x.load@after = $value by sorry
)");

    add(Tactic::SkipStore, R"(
fn #_($x, $target, $value):
    pre %scalar: $x.type.uninterpreted_constant_scalar
@before:
    store $target <- $value
@after:
    prove $x = $target or $x.load@after = $x.load@before by sorry
)");

    add(Tactic::PhiExclusivity, R"(
fn #_():
@phi:
    phi @first, @second
    nop
@first:
    nop
@second:
    prove !@phi.from@first or !@phi.from@second by sorry
)");

    add(Tactic::PhiActivate, R"(
fn #_():
@phi:
    phi @parent
    nop
@parent:
    prove !@phi.from@parent or @phi.active by sorry
)");

    add(Tactic::PhiActiveBackward, R"(
fn #_():
@phi:
    phi @parent
    nop
@parent:
    prove !@phi.from@parent or @parent.active by sorry
)");

    add(Tactic::PhiLoad, R"(
fn #_($x):
    pre %scalar: $x.type.uninterpreted_constant_scalar
@phi:
    phi @parent
    nop
@parent:
    prove !@phi.from@parent or $x.load@phi = $x.load@parent by sorry
)");

    add(Tactic::JumpActiveForward, R"(
fn #_():
@jump:
    jump @phi
    nop
@phi:
    phi @jump
    prove !@jump.active or @phi.from@jump by sorry
)");

    // The label of the target the clause is not about is only named so that the branch can be
    // written down, so it is separated from everything else and never constrains anything
    add(Tactic::BranchActiveForward, R"(
fn #_($cond: bool):
@branch:
    branch $cond, @if_true, @other
    nop
@if_true:
    phi @branch
    nop
@other:
    prove !@branch.active or !$cond or @if_true.from@branch by sorry
)");
    add(Tactic::BranchActiveForward, R"(
fn #_($cond: bool):
@branch:
    branch $cond, @other, @if_false
    nop
@if_false:
    phi @branch
    nop
@other:
    prove !@branch.active or $cond or @if_false.from@branch by sorry
)");

    add(Tactic::BranchDecision, R"(
fn #_($cond: bool):
@branch:
    branch $cond, @if_true, @other
    nop
@if_true:
    phi @branch
    nop
@other:
    prove !@if_true.from@branch or $cond by sorry
)");
    add(Tactic::BranchDecision, R"(
fn #_($cond: bool):
@branch:
    branch $cond, @other, @if_false
    nop
@if_false:
    phi @branch
    nop
@other:
    prove !@if_false.from@branch or !$cond by sorry
)");
}

bool provesProp(const Function& function, Tactic tactic, Bool prop) {
    static const SimpleTactics tactics;
    for (const Pattern& pattern : tactics[tactic]) {
        if (matchClause(pattern, function, prop).has_value())
            return true;
    }
    return false;
}

}
