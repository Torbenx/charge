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
