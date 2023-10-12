#ifndef NODE
#define NODE(kind, type)
#endif

#ifndef EXPR
#define EXPR(kind, type) NODE(kind, type)
#endif
#ifndef STMT
#define STMT(kind, type) NODE(kind, type)
#endif
#ifndef DECL
#define DECL(kind, type) NODE(kind, type)
#endif

NODE(EndScope, EndScope)

// ------ expressions ------

EXPR(ConstraintExpr, UnaryOperatorExpr)
EXPR(LogicalNotExpr, UnaryOperatorExpr)
EXPR(BitwiseNotExpr, UnaryOperatorExpr)
EXPR(PreIncrementExpr, UnaryOperatorExpr)
EXPR(PreDecrementExpr, UnaryOperatorExpr)
EXPR(PlusExpr, UnaryOperatorExpr)
EXPR(NegateExpr, UnaryOperatorExpr)
EXPR(DereferenceExpr, UnaryOperatorExpr)

EXPR(PostIncrementExpr, UnaryOperatorExpr)
EXPR(PostDecrementExpr, UnaryOperatorExpr)

EXPR(AdditionExpr, BinaryOperatorExpr)
EXPR(SubtractionExpr, BinaryOperatorExpr)
EXPR(MultiplyExpr, BinaryOperatorExpr)
EXPR(BitwiseAndExpr, BinaryOperatorExpr)
EXPR(BitwiseXorExpr, BinaryOperatorExpr)
EXPR(BitwiseOrExpr, BinaryOperatorExpr)
EXPR(DivideExpr, BinaryOperatorExpr)
EXPR(RemainderExpr, BinaryOperatorExpr)
EXPR(ShiftLeftExpr, BinaryOperatorExpr)
EXPR(ShiftRightExpr, BinaryOperatorExpr)

EXPR(CompareNotEqualExpr, BinaryOperatorExpr)
EXPR(CompareEqualExpr, BinaryOperatorExpr)
EXPR(CompareLessExpr, BinaryOperatorExpr)
EXPR(CompareLessEqualExpr, BinaryOperatorExpr)
EXPR(CompareGreaterExpr, BinaryOperatorExpr)
EXPR(CompareGreaterEqualExpr, BinaryOperatorExpr)

EXPR(LogicalAndExpr, BinaryLogicalOperatorExpr)
EXPR(LogicalOrExpr, BinaryLogicalOperatorExpr)

EXPR(NumericLiteralExpr, NumericLiteralExpr)
EXPR(CharacterLiteralExpr, CharacterLiteralExpr)

EXPR(CallExpr, CallExpr)
EXPR(IndexExpr, CallExpr)
EXPR(ParenthesizedExpr, ParenthesizedExpr)
EXPR(MemberAccessExpr, AccessExpr)
EXPR(StaticAccessExpr, AccessExpr)
EXPR(IdentifierExpr, IdentifierExpr)
EXPR(CompoundExpr, CompoundExpr)
EXPR(IfExpr, IfExpr)
EXPR(CommaElseExpr, CommaElseExpr)

EXPR(DesignateArgument, DesignateArgument)
EXPR(Parameterize, Parameterize)

// ------ statements ------
STMT(AssignUpdateStmt, UpdateStmt)
STMT(AdditionUpdateStmt, UpdateStmt)
STMT(SubtractionUpdateStmt, UpdateStmt)
STMT(MultiplyUpdateStmt, UpdateStmt)
STMT(BitwiseAndUpdateStmt, UpdateStmt)
STMT(BitwiseXorUpdateStmt, UpdateStmt)
STMT(BitwiseOrUpdateStmt, UpdateStmt)
STMT(DivideUpdateStmt, UpdateStmt)
STMT(RemainderUpdateStmt, UpdateStmt)
STMT(ShiftLeftUpdateStmt, UpdateStmt)
STMT(ShiftRightUpdateStmt, UpdateStmt)

STMT(LogicalAndUpdateStmt, LogicalUpdateStmt)
STMT(LogicalOrUpdateStmt, LogicalUpdateStmt)

STMT(LetStmt, LetStmt)
STMT(LetMutStmt, LetStmt)

STMT(CompoundStmt, CompoundStmt)
STMT(ExpressionStmt, ExpressionStmt)
STMT(IfStmt, IfStmt)

// ------ declarations ------
DECL(TypeDecl, StaticDecl)
DECL(FunctionDecl, StaticDecl)
DECL(StaticVariableDecl, StaticDecl)
DECL(MemberDecl, ParameterOrMemberDecl)
DECL(MutParameterDecl, ParameterOrMemberDecl)
DECL(AmpParameterDecl, ParameterOrMemberDecl)
DECL(ValueParameterDecl, ParameterOrMemberDecl)

#undef EXPR
#undef STMT
#undef DECL
#undef NODE