#ifndef NODE
#define NODE(kind, type, prec)
#endif

#ifndef EXPR
#define EXPR(kind, type, prec) NODE(kind, type, prec)
#endif
#ifndef STMT
#define STMT(kind, type, prec) NODE(kind, type, prec)
#endif

NODE(EmptyNode, EmptyNode, Primary)
NODE(EOS, EOS, EOS)

// ------ expressions ------
EXPR(ConstraintExpr, UnaryOperatorExpr, Unary)
EXPR(LogicalNotExpr, UnaryOperatorExpr, Unary)
EXPR(BitwiseNotExpr, UnaryOperatorExpr, Unary)
EXPR(PreIncrementExpr, UnaryOperatorExpr, Unary)
EXPR(PreDecrementExpr, UnaryOperatorExpr, Unary)
EXPR(PlusExpr, UnaryOperatorExpr, Unary)
EXPR(NegateExpr, UnaryOperatorExpr, Unary)
EXPR(DereferenceExpr, UnaryOperatorExpr, Unary)

EXPR(PostIncrementExpr, UnaryOperatorExpr, Unary)
EXPR(PostDecrementExpr, UnaryOperatorExpr, Unary)

EXPR(AdditionExpr, BinaryOperatorExpr, Addition)
EXPR(SubtractionExpr, BinaryOperatorExpr, Addition)
EXPR(MultiplyExpr, BinaryOperatorExpr, Multiplication)
EXPR(BitwiseAndExpr, BinaryOperatorExpr, BitwiseAnd)
EXPR(BitwiseXorExpr, BinaryOperatorExpr, BitwiseXor)
EXPR(BitwiseOrExpr, BinaryOperatorExpr, BitwiseOr)
EXPR(DivideExpr, BinaryOperatorExpr, Multiplication)
EXPR(RemainderExpr, BinaryOperatorExpr, Multiplication)
EXPR(ShiftLeftExpr, BinaryOperatorExpr, Shift)
EXPR(ShiftRightExpr, BinaryOperatorExpr, Shift)
EXPR(LogicalAndExpr, BinaryOperatorExpr, LogicalAnd)
EXPR(LogicalOrExpr, BinaryOperatorExpr, LogicalOr)

EXPR(CompareNotEqualExpr, BinaryOperatorExpr, Equality)
EXPR(CompareEqualExpr, BinaryOperatorExpr, Equality)
EXPR(CompareLessExpr, BinaryOperatorExpr, Relational)
EXPR(CompareLessEqualExpr, BinaryOperatorExpr, Relational)
EXPR(CompareGreaterExpr, BinaryOperatorExpr, Relational)
EXPR(CompareGreaterEqualExpr, BinaryOperatorExpr, Relational)

EXPR(NumericLiteralExpr, NumericLiteralExpr, Primary)
EXPR(CharacterLiteralExpr, CharacterLiteralExpr, Primary)

EXPR(CallExpr, CallExpr, Postfix)
EXPR(IndexExpr, CallExpr, Postfix)
EXPR(ParenthesizedExpr, ParenthesizedExpr, Primary)
EXPR(MemberAccessExpr, AccessExpr, Postfix)
EXPR(StaticAccessExpr, AccessExpr, Postfix)
EXPR(IdentifierExpr, IdentifierExpr, Primary)
EXPR(CompoundExpr, CompoundExpr, Primary)
EXPR(IfExpr, IfExpr, ConditionalIf)
EXPR(CommaElseExpr, CommaElseExpr, ConditionalElse)

EXPR(DesignateArgument, DesignateArgument, Unary)
EXPR(Parameterize, Parameterize, Postfix)

// ------ statements ------
STMT(AssignStmt, UpdateStmt, Statement)
STMT(AdditionUpdateStmt, UpdateStmt, Statement)
STMT(SubtractionUpdateStmt, UpdateStmt, Statement)
STMT(MultiplyUpdateStmt, UpdateStmt, Statement)
STMT(BitwiseAndUpdateStmt, UpdateStmt, Statement)
STMT(BitwiseXorUpdateStmt, UpdateStmt, Statement)
STMT(BitwiseOrUpdateStmt, UpdateStmt, Statement)
STMT(DivideUpdateStmt, UpdateStmt, Statement)
STMT(RemainderUpdateStmt, UpdateStmt, Statement)
STMT(ShiftLeftUpdateStmt, UpdateStmt, Statement)
STMT(ShiftRightUpdateStmt, UpdateStmt, Statement)
STMT(LogicalAndUpdateStmt, UpdateStmt, Statement)
STMT(LogicalOrUpdateStmt, UpdateStmt, Statement)

STMT(CompoundStmt, CompoundStmt, Primary)
STMT(ExpressionStmt, ExpressionStmt, Statement)
STMT(IfStmt, IfStmt, Statement)
STMT(ElseStmt, ElseStmt, Statement)
STMT(ReturnStmt, ReturnStmt, Statement)
STMT(EmptyReturnStmt, EmptyReturnStmt, Primary)
STMT(LetStmt, LetStmt, Primary)
STMT(VarStmt, LetStmt, Primary)

STMT(InParameter, Parameter, Primary)
STMT(InOutParameter, Parameter, Primary)
STMT(OutParameter, Parameter, Primary)
STMT(LetParameter, Parameter, Primary)
STMT(VarParameter, Parameter, Primary)

#undef EXPR
#undef STMT
#undef NODE