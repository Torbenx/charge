#include <verify/ir/Function.h>

#include <gtest/gtest.h>

namespace verify::ir {

TEST(VerifyIR, UniqueCompoundExpressions) {
    Function fn;
    Expr a = fn.addParameter(Sort::MemoryLoc);
    Expr b = fn.addParameter(Sort::MemoryLoc);

    EXPECT_EQ(fn.addEquality({ a, b }), fn.addEquality({ a, b }));
    EXPECT_NE(fn.addEquality({ a, b }), fn.addEquality({ a, a }));

    // 'a = b' and 'b = a' are intentionally kept as separate expressions
    EXPECT_NE(fn.addEquality({ a, b }), fn.addEquality({ b, a }));
}

TEST(VerifyIR, UniqueSingleWordExpressions) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));
    MemoryLoc other(fn.addParameter(Sort::MemoryLoc));

    EXPECT_EQ(fn.addMemoryLocType({ loc }), fn.addMemoryLocType({ loc }));
    EXPECT_NE(fn.addMemoryLocType({ loc }), fn.addMemoryLocType({ other }));

    // A different kind with the same data must not be confused with 'MemoryLocType'
    Member member = fn.addMemoryLocMember({ loc });
    EXPECT_EQ(member.kind(), ExprKind::MemoryLocMember);
    EXPECT_NE(member.id(), fn.addMemoryLocType({ loc }).id());
}

TEST(VerifyIR, UniqueExpressionsOfDifferentKinds) {
    Function fn;
    Member left(fn.addParameter(Sort::Member));
    Member right(fn.addParameter(Sort::Member));

    // Both kinds store the same two member handles, so they are only distinguished by their kind
    Member compose = fn.addMemberCompose({ left, right });
    Bool contains = fn.addMemberContains({ left, right });

    EXPECT_EQ(compose.kind(), ExprKind::MemberCompose);
    EXPECT_EQ(contains.kind(), ExprKind::MemberContains);
    EXPECT_NE(compose.id(), contains.id());

    EXPECT_EQ(fn.addMemberCompose({ left, right }), compose);
    EXPECT_EQ(fn.addMemberContains({ left, right }), contains);
}

static bool sameList(ExprList a, ExprList b) {
    return a.m_offset == b.m_offset && a.m_size == b.m_size;
}

TEST(VerifyIR, UniqueExpressionLists) {
    Function fn;
    Expr a = fn.addParameter(Sort::MemoryLoc);
    Expr b = fn.addParameter(Sort::MemoryLoc);

    std::array<Expr, 2> ab { a, b };
    std::array<Expr, 2> ba { b, a };
    std::array<Expr, 1> justA { a };

    ExprList abList = fn.makeExprList(ab);
    EXPECT_EQ(abList.size(), 2);
    EXPECT_TRUE(sameList(fn.makeExprList(ab), abList));

    // Order and length are part of the identity of a list
    EXPECT_FALSE(sameList(fn.makeExprList(ba), abList));
    EXPECT_FALSE(sameList(fn.makeExprList(justA), abList));

    ExprList emptyList = fn.makeExprList(std::span<const Expr> {});
    EXPECT_EQ(emptyList.size(), 0);
    EXPECT_TRUE(sameList(fn.makeExprList(std::span<const Expr> {}), emptyList));
    EXPECT_FALSE(sameList(emptyList, abList));
}

TEST(VerifyIR, UniqueExpressionsWithLists) {
    Function fn;
    Fn target(fn.addParameter(Sort::Fn));
    std::array<Expr, 2> args { fn.addParameter(Sort::MemoryLoc), fn.addParameter(Sort::MemoryLoc) };
    std::array<Expr, 2> reversed { args[1], args[0] };

    // Uniquing the list is what allows the expression containing it to be uniqued
    Expr call = fn.addPureScalarReturn({ target, fn.makeExprList(args) });
    EXPECT_EQ(fn.addPureScalarReturn({ target, fn.makeExprList(args) }), call);
    EXPECT_NE(fn.addPureScalarReturn({ target, fn.makeExprList(reversed) }), call);
}

TEST(VerifyIR, FlattenVariadicExpressions) {
    Function fn;
    Bool a(fn.addParameter(Sort::Bool));
    Bool b(fn.addParameter(Sort::Bool));
    Bool c(fn.addParameter(Sort::Bool));
    Bool d(fn.addParameter(Sort::Bool));

    // A nested conjunction contributes its operands instead of itself, on either side
    Bool flat = fn.addAnd({ a, b, c });
    EXPECT_TRUE(std::ranges::equal(fn.view(fn.getAnd(flat).operands), std::array<Expr, 3> { a, b, c }));
    EXPECT_EQ(fn.addAnd({ fn.addAnd({ a, b }), c }), flat);
    EXPECT_EQ(fn.addAnd({ a, fn.addAnd({ b, c }) }), flat);
    EXPECT_EQ(fn.addAnd({ fn.addAnd({ a, b }), fn.addAnd({ c, d }) }), fn.addAnd({ a, b, c, d }));

    // Only the operands of the same kind are spliced
    Bool orOfCd = fn.addOr({ c, d });
    Bool mixed = fn.addAnd({ a, orOfCd });
    EXPECT_TRUE(std::ranges::equal(fn.view(fn.getAnd(mixed).operands), std::array<Expr, 2> { a, orOfCd }));

    // A negated operand denotes the negation of the whole conjunction, so it is not spliced
    Bool negated = fn.addAnd({ a, !fn.addAnd({ b, c }) });
    EXPECT_TRUE(std::ranges::equal(fn.view(fn.getAnd(negated).operands),
        std::array<Expr, 2> { a, !fn.addAnd({ b, c }) }));
}

TEST(VerifyIR, VariadicExpressionsAreNotNormalized) {
    Function fn;
    Bool a(fn.addParameter(Sort::Bool));
    Bool b(fn.addParameter(Sort::Bool));

    // Neither the order nor repeated operands nor literals are touched
    EXPECT_NE(fn.addAnd({ a, b }), fn.addAnd({ b, a }));
    EXPECT_EQ(fn.view(fn.getAnd(fn.addAnd({ a, a })).operands).size(), 2);
    EXPECT_EQ(fn.view(fn.getOr(fn.addOr({ a, Bool(false) })).operands).size(), 2);

    // 'and' and 'or' over the same operands are distinct expressions
    Bool conjunction = fn.addAnd({ a, b });
    Bool disjunction = fn.addOr({ a, b });
    EXPECT_EQ(conjunction.kind(), ExprKind::And);
    EXPECT_EQ(disjunction.kind(), ExprKind::Or);
    EXPECT_NE(conjunction.id(), disjunction.id());

    // Like every other expression they are uniqued, and negating one keeps its identity
    EXPECT_EQ(fn.addAnd({ a, b }), conjunction);
    EXPECT_EQ((!conjunction).id(), conjunction.id());
    EXPECT_NE(!conjunction, conjunction);
    EXPECT_EQ(fn.sortOf(conjunction), Sort::Bool);
}

TEST(VerifyIR, EnumerateExpressions) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));

    // Parameters are inline expressions and are not part of the enumeration
    EXPECT_EQ(fn.expressionCount(), 0);
    EXPECT_TRUE(std::ranges::empty(fn.expressions()));

    std::vector<Expr> added {
        fn.addMemoryLocType({ loc }),
        fn.addTypeLoad({ loc, CodePos(0) }),
        fn.addEquality({ loc, loc }),
    };
    // Adding a known expression again must not enumerate it twice
    fn.addMemoryLocType({ loc });

    EXPECT_EQ(fn.expressionCount(), 3);
    EXPECT_TRUE(std::ranges::equal(fn.expressions(), added));
}

TEST(VerifyIR, SortOfExpressions) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));
    Type type(fn.addParameter(Sort::Type));

    // Parameters are the only expressions whose sort is looked up instead of being known statically
    EXPECT_EQ(fn.sortOf(loc), Sort::MemoryLoc);
    EXPECT_EQ(fn.sortOf(type), Sort::Type);

    EXPECT_EQ(fn.sortOf(fn.addEquality({ loc, loc })), Sort::Bool);
    EXPECT_EQ(fn.sortOf(Bool(true)), Sort::Bool);
    EXPECT_EQ(fn.sortOf(fn.addMemoryLocDecl({ loc })), Sort::MemoryDecl);
    EXPECT_EQ(fn.sortOf(fn.addMemoryLocMember({ loc })), Sort::Member);
}

TEST(VerifyIR, SortOfLoads) {
    Function fn;
    MemoryLoc loc(fn.addParameter(Sort::MemoryLoc));
    RelativeCodePos pos = CodePos(0);

    // Every load carries its sort in its kind
    EXPECT_EQ(fn.sortOf(fn.addBoolLoad({ loc, pos })), Sort::Bool);
    EXPECT_EQ(fn.sortOf(fn.addTypeLoad({ loc, pos })), Sort::Type);
    EXPECT_EQ(fn.sortOf(fn.addMemoryLocLoad({ loc, pos })), Sort::MemoryLoc);

    // Loads of different sorts stay distinct even though their data is identical
    Expr boolLoad = fn.addBoolLoad({ loc, pos });
    Expr typeLoad = fn.addTypeLoad({ loc, pos });
    EXPECT_NE(boolLoad, typeLoad);
    EXPECT_NE(boolLoad.id(), typeLoad.id());

    EXPECT_EQ(fn.addLoad(Sort::Type, { loc, pos }), typeLoad);
    EXPECT_TRUE(isLoad(typeLoad.kind()));
    EXPECT_FALSE(isLoad(ExprKind::Equality));
    EXPECT_EQ(fn.getLoad(typeLoad).loc, loc);
}

TEST(VerifyIR, ScalarSort) {
    Function fn;
    Type type(fn.addParameter(Sort::Type));
    Type other(fn.addParameter(Sort::Type));

    // A 'scalarType' expression alone says nothing, only a theorem for it does
    Bool scalarType = fn.addScalarType({ type, Sort::Type });
    EXPECT_FALSE(fn.scalarSort(type).has_value());

    fn.addTheorem(scalarType, CodePos(0), Proof::makeSorry());
    EXPECT_EQ(fn.scalarSort(type).value(), Sort::Type);
    EXPECT_FALSE(fn.scalarSort(other).has_value());

    // Looking a 'ScalarType' up must not create it
    EXPECT_FALSE(fn.findScalarType({ type, Sort::Bool }).has_value());
    EXPECT_FALSE(fn.scalarSort(type).value() == Sort::Bool);
}

TEST(VerifyIR, FindTheorem) {
    Function fn;
    Expr a = fn.addParameter(Sort::MemoryLoc);
    Expr b = fn.addParameter(Sort::MemoryLoc);
    Bool aEqB = fn.addEquality({ a, b });
    Bool aEqA = fn.addEquality({ a, a });

    EXPECT_FALSE(fn.findTheorem(aEqB).has_value());

    Theorem theorem = fn.addTheorem(aEqB, CodePos(0), Proof::makeSorry());
    EXPECT_EQ(fn.findTheorem(aEqB).value(), theorem);
    EXPECT_EQ(fn.prop(theorem), aEqB);
    EXPECT_EQ(fn.proof(theorem).tactic(), Tactic::Sorry);

    // A proposition and its negation are proven by separate theorems
    EXPECT_FALSE(fn.findTheorem(!aEqB).has_value());
    Theorem negated = fn.addTheorem(!aEqB, CodePos(0), Proof::makeSorry());
    EXPECT_NE(negated, theorem);
    EXPECT_EQ(fn.findTheorem(!aEqB).value(), negated);
    EXPECT_EQ(fn.findTheorem(aEqB).value(), theorem);

    EXPECT_FALSE(fn.findTheorem(aEqA).has_value());
}

TEST(VerifyIR, FindPreCondition) {
    Function fn;
    Expr a = fn.addParameter(Sort::MemoryLoc);
    Expr b = fn.addParameter(Sort::MemoryLoc);
    Bool aEqB = fn.addEquality({ a, b });

    // Preconditions are uniqued together with the other theorems
    Theorem precondition = fn.addPreCondition(aEqB, CodePos(0));
    EXPECT_EQ(fn.findTheorem(aEqB).value(), precondition);
    EXPECT_EQ(fn.proof(precondition).tactic(), Tactic::Precondition);
}

TEST(VerifyIR, UniqueExpressionsRespectNegation) {
    Function fn;
    Bool cond = Expr::makePositionActive(CodePos(0));
    Bool otherCond = Expr::makePositionActive(CodePos(1));

    Bool equal = fn.addEquality({ cond, otherCond });
    Bool negated = fn.addEquality({ cond, !otherCond });
    EXPECT_NE(equal, negated);
    EXPECT_EQ(fn.addEquality({ cond, !otherCond }), negated);

    // The negation bit of the result is not part of the stored expression
    EXPECT_EQ(fn.addEquality({ cond, otherCond }), equal);
    EXPECT_EQ((!equal).id(), equal.id());
}

}
