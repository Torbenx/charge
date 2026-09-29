#include <verify/language/Formatter.h>
#include <verify/language/FunctionParser.h>

#include <gtest/gtest.h>

namespace verify::language {

static ir::Function parseForTest(const char* source) {
    return parseFunction(source).function;
}

TEST(VerifyLanguage, ParseFunctionDefinition) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- $b
    store $a <- $b
    store $a <- $b
    store $a <- $b
    store $a <- $b
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 5);
}

TEST(VerifyLanguage, ParseParameterSorts) {
    ir::Function fn = parseForTest(R"(
fn #test($a: bool, $b, $c: memory_decl):
    nop
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.sortOf(ir::Expr(ir::ExprKind::FunctionParameter, 0)), ir::Sort::Bool);
    // A parameter without a sort is a memory location
    EXPECT_EQ(fn.sortOf(ir::Expr(ir::ExprKind::FunctionParameter, 1)), ir::Sort::MemoryLoc);
    EXPECT_EQ(fn.sortOf(ir::Expr(ir::ExprKind::FunctionParameter, 2)), ir::Sort::MemoryDecl);

    EXPECT_THROW(parseForTest(R"(
fn #test($a: not_a_sort):
    nop
)"),
        ParserException);
}

TEST(VerifyLanguage, ParseNop) {
    ir::Function fn = parseForTest(R"(
fn #test():
@first:
    nop
@second:
    nop
)");
    // A nop occupies a code position without doing anything
    EXPECT_EQ(fn.here().id(), 2);
    EXPECT_EQ(fn.opcodeAt(ir::CodePos(0)), ir::Opcode::Nop);
    EXPECT_EQ(fn.opcodeAt(ir::CodePos(1)), ir::Opcode::Nop);
}

TEST(VerifyLanguage, ParseNames) {
    ParsedFunction parsed = parseFunction(R"(
fn #test($a, $b):
@entry:
    jump @exit
@exit:
@second_name:
    nop
    prove %a_eq_b: $a = $b by sorry
    prove $b = $a by sorry
@end:
    nop
    prove %never_true: !@entry.active by sorry
)");
    EXPECT_EQ(parsed.name, "test");
    EXPECT_EQ(parsed.parameterNames, (std::vector<std::string> { "a", "b" }));
    // Labels are indexed by position and a position keeps the first of its names. The table
    // reaches one past the last instruction, where a label may sit in front of theorems.
    EXPECT_EQ(parsed.labels, (std::vector<std::string> { "entry", "exit", "end", "" }));
    // A theorem the source did not name has no name here either
    EXPECT_EQ(parsed.theoremNames, (std::vector<std::string> { "a_eq_b", "", "never_true" }));
}

TEST(VerifyLanguage, ParseUndefinedLabel) {
    // A label that is referenced but never defined leaves the jump without a target
    EXPECT_THROW(parseFunction(R"(
fn #test():
    jump @nowhere
)"),
        ParserException);
}

TEST(VerifyLanguage, ParseStore) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    store $a <- $b
)");
    EXPECT_EQ(fn.parameterCount(), 2);
    EXPECT_EQ(fn.here().id(), 1);
    ir::CodePos storePos(0);
    EXPECT_EQ(fn.getStore(storePos).loc, ir::Expr(ir::ExprKind::FunctionParameter, 0));
    EXPECT_EQ(fn.getStore(storePos).value, ir::Expr(ir::ExprKind::FunctionParameter, 1));
}

TEST(VerifyLanguage, ParseBranchAndPhi) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
@entry:
    jump @before
@before:
    phi @entry, @branch
@branch:
    branch $a = $b, @before, @after
@after:
    phi @branch
)");
    EXPECT_EQ(fn.parameterCount(), 2);
    EXPECT_EQ(fn.here().id(), 4);
    ir::CodePos jumpPos(0);
    ir::CodePos phi1Pos(1);
    ir::CodePos branchPos(2);
    ir::CodePos phi2Pos(3);

    EXPECT_EQ(fn.getJump(jumpPos).target, phi1Pos);

    EXPECT_EQ(fn.incomingEdges(phi1Pos).size(), 2);
    EXPECT_EQ(fn.edgeSource(fn.incomingEdges(phi1Pos).at(0)), jumpPos);
    EXPECT_EQ(fn.edgeSource(fn.incomingEdges(phi1Pos).at(1)), branchPos);

    EXPECT_EQ(fn.getEquality(fn.getBranch(branchPos).cond).left, ir::Expr(ir::ExprKind::FunctionParameter, 0));
    EXPECT_EQ(fn.getEquality(fn.getBranch(branchPos).cond).right, ir::Expr(ir::ExprKind::FunctionParameter, 1));
    EXPECT_EQ(fn.getBranch(branchPos).ifTrue, phi1Pos);
    EXPECT_EQ(fn.getBranch(branchPos).ifFalse, phi2Pos);

    EXPECT_EQ(fn.incomingEdges(phi2Pos).size(), 1);
    EXPECT_EQ(fn.edgeSource(fn.incomingEdges(phi2Pos).at(0)), branchPos);
}

TEST(VerifyLanguage, ParseMultilineExpression) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- $b = $c
    store $a <- $b
        = $c
    store $a <- $b =
        $c
    store $a
        <- $b = $c
    store
        $a <- $b = $c
    store
        $a
        <-
            $b
        =
            $c
)");
    auto testExpr = [&fn](ir::CodePos pos) {
        EXPECT_EQ(fn.getStore(pos).loc, ir::Expr(ir::ExprKind::FunctionParameter, 0));
        EXPECT_EQ(fn.getEquality((ir::Bool)fn.getStore(pos).value).left, ir::Expr(ir::ExprKind::FunctionParameter, 1));
        EXPECT_EQ(fn.getEquality((ir::Bool)fn.getStore(pos).value).right, ir::Expr(ir::ExprKind::FunctionParameter, 2));
    };
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 6);
    testExpr(ir::CodePos(0));
    testExpr(ir::CodePos(1));
    testExpr(ir::CodePos(2));
    testExpr(ir::CodePos(3));
    testExpr(ir::CodePos(4));
    testExpr(ir::CodePos(5));
}

TEST(VerifyLanguage, ParseConnectivePrecedence) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c, $d, $e, $f):
    store $a <- $a = $b and $c = $d or $e = $f
    store $a <- ($a = $b and $c = $d) or ($e = $f)
    store $a <- $a = $b and ($c = $d or $e = $f)
)");
    // 'or' binds weakest, so the first two stores hold the same expression
    ir::Bool first = (ir::Bool)fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(first.kind(), ir::ExprKind::Or);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(1)).value, first);

    auto disjuncts = fn.view(fn.getOr(first).operands);
    EXPECT_EQ(disjuncts.size(), 2);
    EXPECT_EQ(disjuncts[0].kind(), ir::ExprKind::And);
    EXPECT_EQ(disjuncts[1].kind(), ir::ExprKind::Equality);

    // The operands of the 'and' are the equalities, which bind strongest
    auto conjuncts = fn.view(fn.getAnd((ir::Bool)disjuncts[0]).operands);
    EXPECT_EQ(conjuncts.size(), 2);
    EXPECT_EQ(conjuncts[0].kind(), ir::ExprKind::Equality);
    EXPECT_EQ(conjuncts[1].kind(), ir::ExprKind::Equality);

    // Parentheses give the 'or' the higher precedence instead
    ir::Bool grouped = (ir::Bool)fn.getStore(ir::CodePos(2)).value;
    EXPECT_EQ(grouped.kind(), ir::ExprKind::And);
    EXPECT_EQ(fn.view(fn.getAnd(grouped).operands)[1].kind(), ir::ExprKind::Or);
}

TEST(VerifyLanguage, ParseConnectiveAssociativity) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- $a = $a or $b = $b or $c = $c
    store $a <- ($a = $a or $b = $b) or $c = $c
    store $a <- $a = $a or ($b = $b or $c = $c)
    store $a <- $a = $a or !($b = $b or $c = $c)
)");
    // A chain of the same connective is one expression, however it is grouped
    ir::Bool chain = (ir::Bool)fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(fn.view(fn.getOr(chain).operands).size(), 3);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(1)).value, chain);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(2)).value, chain);

    // A negated operand is not merged into the surrounding disjunction
    ir::Bool negated = (ir::Bool)fn.getStore(ir::CodePos(3)).value;
    EXPECT_NE(negated, chain);
    auto operands = fn.view(fn.getOr(negated).operands);
    EXPECT_EQ(operands.size(), 2);
    EXPECT_EQ(operands[1].kind(), ir::ExprKind::Or);
    EXPECT_EQ((uint32_t)operands[1].boolNegatedBit, 1u);
}

TEST(VerifyLanguage, ParseMultilineConnective) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    prove $a = $b
        or $b = $c
        or $a = $c by eq_transitive
    store $a <- $a = $b or $b = $c or $a = $c
)");
    // Continuation lines do not change the expression
    ir::Bool prop = fn.prop(ir::Theorem(0));
    EXPECT_EQ(prop.kind(), ir::ExprKind::Or);
    EXPECT_EQ(fn.view(fn.getOr(prop).operands).size(), 3);
    EXPECT_EQ(fn.proof(ir::Theorem(0)).tactic(), ir::Tactic::EqualityTransitive);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(0)).value, prop);
}

TEST(VerifyLanguage, ParseOperatorSymbols) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c, $d):
    store $a <- $a = $b and $c = $d or $a = $c
    store $a <- $a = $b ∧ $c = $d ∨ $a = $c
    store $a <- $a = $b ∧ $c = $d or $a = $c
    store $a <- !($a = $b)
    store $a <- ¬($a = $b)
    store $a <- $a != $b
    store $a <- $a ≠ $b
)");
    // A symbol stands for the operator it spells, whichever way the rest is written
    ir::Expr connectives = fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(fn.getStore(ir::CodePos(1)).value, connectives);
    EXPECT_EQ(fn.getStore(ir::CodePos(2)).value, connectives);

    ir::Expr negated = fn.getStore(ir::CodePos(3)).value;
    EXPECT_EQ(fn.getStore(ir::CodePos(4)).value, negated);
    EXPECT_EQ(fn.getStore(ir::CodePos(5)).value, negated);
    EXPECT_EQ(fn.getStore(ir::CodePos(6)).value, negated);
}

TEST(VerifyLanguage, ParseSymbolsWithoutSpaces) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c, $d):
    store $a <- $a = $b ∧ ¬($c = $d)
    store $a <- $a=$b∧¬($c=$d)
)");
    // A symbol is no part of a name, so it separates the names around it on its own
    EXPECT_EQ(fn.getStore(ir::CodePos(1)).value, fn.getStore(ir::CodePos(0)).value);
}

TEST(VerifyLanguage, ParseMultilineSymbolConnective) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    prove $a = $b
        ∨ $b = $c
        ∨ $a = $c by eq_transitive
    store $a <- $a = $b or $b = $c or $a = $c
)");
    // A continuation line may begin with a symbol, as it may with the word it stands for
    ir::Bool prop = fn.prop(ir::Theorem(0));
    EXPECT_EQ(prop.kind(), ir::ExprKind::Or);
    EXPECT_EQ(fn.view(fn.getOr(prop).operands).size(), 3);
    EXPECT_EQ((ir::Bool)fn.getStore(ir::CodePos(0)).value, prop);
}

TEST(VerifyLanguage, ParseConnectiveInSatClause) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    prove true by sat:
        clause $a = $b or $b = $a by sorry
        clause $a = $a and $b = $b by eq_reflexive
)");
    auto& proof = fn.getSat(fn.proof(ir::Theorem(2)));
    EXPECT_EQ(proof.clauses.size(), 2);

    // 'by' ends the clause expression, it is not read as another operand
    EXPECT_EQ(fn.prop(proof.clauses[0]).kind(), ir::ExprKind::Or);
    EXPECT_EQ(fn.proof(proof.clauses[0]).tactic(), ir::Tactic::Sorry);
    EXPECT_EQ(fn.prop(proof.clauses[1]).kind(), ir::ExprKind::And);
    EXPECT_EQ(fn.proof(proof.clauses[1]).tactic(), ir::Tactic::EqualityReflexive);
}

TEST(VerifyLanguage, ParseTheorems) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    pre %a_eq_b: $a = $b
    prove %reflex: $a = $a by eq_reflexive
    prove %transitive: $a != $b by eq_transitive
    prove %sorry_thm: $a = $c by sorry
    prove $b = $c by load_store
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 0);

    ir::Theorem preTheorem(0);
    EXPECT_EQ(fn.position(preTheorem), ir::CodePos(0));
    EXPECT_EQ(fn.proof(preTheorem).tactic(), ir::Tactic::Precondition);
    EXPECT_EQ(fn.getEquality(fn.prop(preTheorem)).left, ir::Expr(ir::ExprKind::FunctionParameter, 0));
    EXPECT_EQ(fn.getEquality(fn.prop(preTheorem)).right, ir::Expr(ir::ExprKind::FunctionParameter, 1));

    ir::Theorem reflexTheorem(1);
    EXPECT_EQ(fn.proof(reflexTheorem).tactic(), ir::Tactic::EqualityReflexive);

    ir::Theorem transitiveTheorem(2);
    EXPECT_EQ(fn.proof(transitiveTheorem).tactic(), ir::Tactic::EqualityTransitive);

    ir::Theorem sorryTheorem(3);
    EXPECT_EQ(fn.proof(sorryTheorem).tactic(), ir::Tactic::Sorry);

    ir::Theorem loadStoreTheorem(4);
    EXPECT_EQ(fn.proof(loadStoreTheorem).tactic(), ir::Tactic::LoadStore);
}

TEST(VerifyLanguage, ParseLoad) {
    // The sort of a load is not spelled out, it follows from the 'scalarType' theorem
    ir::Function fn = parseForTest(R"(
fn #test($x, $y):
@entry:
    pre %x_scalar: $x.type.memory_loc_scalar
    store $y <- $x.load@entry
)");
    EXPECT_EQ(fn.here().id(), 1);

    ir::Expr value = fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(value.kind(), ir::ExprKind::MemoryLocLoad);
    EXPECT_EQ(fn.sortOf(value), ir::Sort::MemoryLoc);
    EXPECT_EQ(fn.getLoad(value).loc, ir::Expr(ir::ExprKind::FunctionParameter, 0));

    // A load of a location that was never proven scalar cannot be given a sort
    EXPECT_THROW(parseForTest(R"(
fn #test($x, $y):
@entry:
    store $y <- $x.load@entry
)"),
        ParserException);
}

TEST(VerifyLanguage, ParseDuplicateTheorems) {
    // Theorems are uniqued by their proposition, so proving one twice is an error
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove $a = $b by sorry
    prove $a = $b by load_store
)"),
        ParserException);

    // Preconditions share the same propositions as the other theorems
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    pre %a_eq_b: $a = $b
    prove $a = $b by load_store
)"),
        ParserException);

    // A proposition and its negation are distinct
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    prove $a = $b by sorry
    prove $a != $b by sorry
)");
    EXPECT_EQ(fn.prop(ir::Theorem(0)), !fn.prop(ir::Theorem(1)));
}

TEST(VerifyLanguage, ParseSatProof) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    prove true by sat:
        clause $a = $a by eq_reflexive
        clause $b = $b by eq_reflexive
)");
    EXPECT_EQ(fn.parameterCount(), 2);
    EXPECT_EQ(fn.here().id(), 0);

    // Writing a clause down states a theorem of its own, which comes before the one it proves
    ir::Theorem satTheorem(2);
    EXPECT_EQ(fn.position(satTheorem), ir::CodePos(0));
    EXPECT_EQ(fn.proof(satTheorem).tactic(), ir::Tactic::Sat);
    EXPECT_EQ(fn.prop(satTheorem), ir::Bool(true));
    auto& proof = fn.getSat(fn.proof(satTheorem));
    EXPECT_EQ(proof.clauses, (std::vector<ir::Theorem> { ir::Theorem(0), ir::Theorem(1) }));
    for (ir::Theorem clause : proof.clauses) {
        EXPECT_EQ(fn.position(clause), ir::CodePos(0));
        EXPECT_EQ(fn.prop(clause).kind(), ir::ExprKind::Equality);
        EXPECT_EQ(fn.proof(clause).tactic(), ir::Tactic::EqualityReflexive);
    }
}

TEST(VerifyLanguage, ParseSatProofOfStatedTheorem) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    prove %a_eq_b: $a = $b by sorry
    prove true by sat:
        clause %a_eq_b
        clause $b = $b by eq_reflexive
)");
    // Naming a theorem refers to it, it does not state a second one
    auto& proof = fn.getSat(fn.proof(ir::Theorem(2)));
    EXPECT_EQ(proof.clauses, (std::vector<ir::Theorem> { ir::Theorem(0), ir::Theorem(1) }));

    // A theorem has to be stated before a clause can name it
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove true by sat:
        clause %a_eq_b
    prove %a_eq_b: $a = $b by sorry
)"),
        ParserException);

    // A proof cannot rest on the theorem it establishes
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove %circular: $a = $b by sat:
        clause %circular
)"),
        ParserException);

    // Restating the proposition of a theorem is an error for a clause as well
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove %a_eq_b: $a = $b by sorry
    prove true by sat:
        clause $a = $b by eq_reflexive
)"),
        ParserException);

    // The clauses are stated first, so a clause restating the proposition of the theorem
    // they prove is caught the same way round
    EXPECT_THROW(parseForTest(R"(
fn #test($a, $b):
    prove $a = $b by sat:
        clause $a = $b by eq_reflexive
)"),
        ParserException);
}

TEST(VerifyLanguage, ParseUniqueExpressions) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- $b = $c
    store $a <- $b = $c
    store $a <- $c = $b
    store $a <- $b = $b
    store $a <- ($b = $c) = ($b = $c)
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 5);

    // Writing the same expression twice must not create a second expression
    ir::Expr bEqC = fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(fn.getStore(ir::CodePos(1)).value, bEqC);

    // The operands and their order are part of the identity of the expression
    EXPECT_NE(fn.getStore(ir::CodePos(2)).value, bEqC);
    EXPECT_NE(fn.getStore(ir::CodePos(3)).value, bEqC);

    // Nested expressions are uniqued as well
    auto nested = fn.getEquality((ir::Bool)fn.getStore(ir::CodePos(4)).value);
    EXPECT_EQ(nested.left, bEqC);
    EXPECT_EQ(nested.right, bEqC);
}

TEST(VerifyLanguage, ParseUniqueCallArguments) {
    ir::Function fn = parseForTest(R"(
fn #test($f, $a, $b):
    call $f($a, $b)
    call $f($a, $b)
    call $f($b, $a)
    call $f()
    call $f()
)");
    EXPECT_EQ(fn.parameterCount(), 3);
    EXPECT_EQ(fn.here().id(), 5);

    auto args = [&fn](uint32_t pos) { return fn.getCall(ir::CodePos(pos)).args; };
    auto sameList = [](ir::ExprList a, ir::ExprList b) {
        return a.m_offset == b.m_offset && a.m_size == b.m_size;
    };

    // Identical argument lists are shared between the calls
    EXPECT_EQ(args(0).size(), 2);
    EXPECT_TRUE(sameList(args(1), args(0)));
    EXPECT_FALSE(sameList(args(2), args(0)));

    EXPECT_EQ(args(3).size(), 0);
    EXPECT_TRUE(sameList(args(4), args(3)));
}

TEST(VerifyLanguage, ParseSetConstructors) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    store $a <- mset($a)
    store $a <- incl_iset($a)
    store $a <- excl_iset($a)
    store $a <- path_iset($a)
    store $a <- mset()
    store $a <- iset()
)");
    auto value = [&fn](uint32_t pos) { return fn.getStore(ir::CodePos(pos)).value; };
    ir::Expr a(ir::ExprKind::FunctionParameter, 0);

    EXPECT_EQ(value(0).kind(), ir::ExprKind::LocationMemorySet);
    EXPECT_EQ(fn.getLocationMemorySet((ir::MemorySet)value(0)).loc, a);
    EXPECT_EQ(value(1).kind(), ir::ExprKind::InclusiveInvariantSet);
    EXPECT_EQ(value(2).kind(), ir::ExprKind::ExclusiveInvariantSet);
    EXPECT_EQ(value(3).kind(), ir::ExprKind::PathInvariantSet);

    // The three invariant sets of a location share a sort with the empty one
    EXPECT_EQ(fn.sortOf(value(0)), ir::Sort::MemorySet);
    EXPECT_EQ(fn.sortOf(value(1)), ir::Sort::InvariantSet);
    EXPECT_EQ(fn.sortOf(value(2)), ir::Sort::InvariantSet);
    EXPECT_EQ(fn.sortOf(value(3)), ir::Sort::InvariantSet);

    // An empty set is one expression per set sort, it carries nothing beyond the sort
    EXPECT_EQ(value(4), fn.emptySet(ir::Sort::MemorySet));
    EXPECT_EQ(value(5), fn.emptySet(ir::Sort::InvariantSet));
    EXPECT_NE(value(4), value(5));

    // 'iset' only writes the empty set, an invariant set of a location says which one it is
    EXPECT_THROW(parseForTest("fn #test($a):\n    store $a <- iset($a)\n"), ParserException);
    EXPECT_THROW(parseForTest("fn #test($a):\n    store $a <- incl_iset()\n"), ParserException);
    EXPECT_THROW(parseForTest("fn #test($a):\n    store $a <- mset $a\n"), ParserException);
    EXPECT_THROW(parseForTest("fn #test($a):\n    store $a <- mset($a\n"), ParserException);
}

TEST(VerifyLanguage, ParseSetOperatorPrecedence) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c, $d):
    store $a <- mset($a) union mset($b) setminus mset($c) intersection mset($d)
    store $a <- mset($a) union (mset($b) setminus (mset($c) intersection mset($d)))
    store $a <- mset($a) ∪ mset($b) ∖ mset($c) ∩ mset($d)
)");
    // 'intersection' binds strongest, then 'setminus', then 'union'
    ir::Expr chain = fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(fn.getStore(ir::CodePos(1)).value, chain);
    // A symbol stands for the operator it spells, so it groups the same way
    EXPECT_EQ(fn.getStore(ir::CodePos(2)).value, chain);

    EXPECT_EQ(chain.kind(), ir::ExprKind::MemorySetUnion);
    auto operands = fn.view(fn.getSetOperands(chain));
    EXPECT_EQ(operands.size(), 2);
    EXPECT_EQ(operands[0].kind(), ir::ExprKind::LocationMemorySet);
    EXPECT_EQ(operands[1].kind(), ir::ExprKind::MemorySetMinus);

    auto setMinus = fn.getSetMinus(operands[1]);
    EXPECT_EQ(setMinus.base.kind(), ir::ExprKind::LocationMemorySet);
    EXPECT_EQ(setMinus.subtrahend.kind(), ir::ExprKind::MemorySetIntersection);
    EXPECT_EQ(fn.view(fn.getSetOperands(setMinus.subtrahend)).size(), 2);
}

TEST(VerifyLanguage, ParseSetOperatorsBindTighterThanEquality) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- mset($a) union mset($b) = mset($c)
    store $a <- (mset($a) union mset($b)) = mset($c)
)");
    ir::Bool equality = (ir::Bool)fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(fn.getStore(ir::CodePos(1)).value, equality);
    EXPECT_EQ(equality.kind(), ir::ExprKind::Equality);
    EXPECT_EQ(fn.getEquality(equality).left.kind(), ir::ExprKind::MemorySetUnion);
}

TEST(VerifyLanguage, ParseSetOperatorAssociativity) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b, $c):
    store $a <- mset($a) union mset($b) union mset($c)
    store $a <- (mset($a) union mset($b)) union mset($c)
    store $a <- mset($a) union (mset($b) union mset($c))
    store $a <- mset($a) setminus mset($b) setminus mset($c)
    store $a <- (mset($a) setminus mset($b)) setminus mset($c)
    store $a <- mset($a) setminus (mset($b) setminus mset($c))
)");
    // A chain of unions is one expression, however it is grouped
    ir::Expr chain = fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(fn.view(fn.getSetOperands(chain)).size(), 3);
    EXPECT_EQ(fn.getStore(ir::CodePos(1)).value, chain);
    EXPECT_EQ(fn.getStore(ir::CodePos(2)).value, chain);

    // 'setminus' is not associative, so a chain of it groups to the left and no further
    ir::Expr nested = fn.getStore(ir::CodePos(3)).value;
    EXPECT_EQ(fn.getStore(ir::CodePos(4)).value, nested);
    EXPECT_NE(fn.getStore(ir::CodePos(5)).value, nested);
    EXPECT_EQ(fn.getSetMinus(nested).base.kind(), ir::ExprKind::MemorySetMinus);
}

TEST(VerifyLanguage, ParseInvariantSetOperators) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    store $a <- incl_iset($a) ∖ path_iset($b) ∪ iset()
)");
    // The operators exist once per set sort, the sort of the operands decides which one is meant
    ir::Expr chain = fn.getStore(ir::CodePos(0)).value;
    EXPECT_EQ(chain.kind(), ir::ExprKind::InvariantSetUnion);
    EXPECT_EQ(fn.sortOf(chain), ir::Sort::InvariantSet);
    EXPECT_EQ(fn.view(fn.getSetOperands(chain))[0].kind(), ir::ExprKind::InvariantSetMinus);
}

TEST(VerifyLanguage, ParseSetOperatorOfNonSet) {
    // Only a set has set operations, the sort of the first operand is what says so
    EXPECT_THROW(parseForTest("fn #test($a, $b):\n    store $a <- $a union $b\n"), ParserException);
    EXPECT_THROW(parseForTest("fn #test($a, $b):\n    store $a <- $a setminus $b\n"), ParserException);
    EXPECT_THROW(parseForTest("fn #test($a, $b):\n    store $a <- $a intersection $b\n"), ParserException);
}

TEST(VerifyLanguage, ParseEqualityNegation) {
    ir::Function fn = parseForTest(R"(
fn #test($a, $b):
    store $a <- $a = $b
    store $a <- $a != $b
)");
    EXPECT_EQ(fn.parameterCount(), 2);
    EXPECT_EQ(fn.here().id(), 2);

    ir::Bool equal = (ir::Bool)fn.getStore(ir::CodePos(0)).value;
    ir::Bool notEqual = (ir::Bool)fn.getStore(ir::CodePos(1)).value;

    // '!=' reuses the expression of '=' and only differs in the negation bit of the handle
    EXPECT_EQ(equal.kind(), ir::ExprKind::Equality);
    EXPECT_EQ(notEqual.kind(), ir::ExprKind::Equality);
    EXPECT_EQ(equal.id(), notEqual.id());
    EXPECT_EQ((uint32_t)equal.boolNegatedBit, 0u);
    EXPECT_EQ((uint32_t)notEqual.boolNegatedBit, 1u);
    EXPECT_NE(equal, notEqual);
    EXPECT_EQ(!equal, notEqual);
}

TEST(VerifyLanguage, ParseContinuationBeginningWithLabel) {
    // A line may continue an instruction with a reference to a label
    ParsedFunction oneLine = parseFunction(R"(
fn #test($a, $b):
@entry:
    jump @loop
@loop:
    phi @entry, @branch
    store $a <- @entry.active or @loop.active
@branch:
    branch $a = $b, @loop, @exit
@exit:
    phi @branch
)");
    ParsedFunction wrapped = parseFunction(R"(
fn #test($a, $b):
@entry:
    jump @loop
@loop:
    phi @entry,
        @branch
    store $a <- @entry.active or
        @loop.active
@branch:
    branch $a = $b, @loop,
        @exit
@exit:
    phi @branch
)");
    EXPECT_EQ(wrapped.labels, oneLine.labels);
    EXPECT_EQ(format(wrapped), format(oneLine));

    // Without the ':' a line is no label, and nothing else begins a body with a label reference
    EXPECT_THROW(parseFunction("fn #test($a):\n@label\n    nop\n"), ParserException);
}

}