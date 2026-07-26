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
