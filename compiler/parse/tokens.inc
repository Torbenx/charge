#ifndef TOKEN
#define TOKEN(kind, type, prec)
#endif

#ifndef EXPR
#define EXPR(kind, type, prec) TOKEN(kind, type, prec)
#endif
#ifndef STMT
#define STMT(kind, type) TOKEN(kind, type, Statement)
#endif
#ifndef DECL
#define DECL(kind, type) TOKEN(kind, type, Statement)
#endif
#ifndef ATTRIBUTE
#define ATTRIBUTE(kind) TOKEN(kind, Attribute, Statement)
#endif

TOKEN(FunctionBody, FunctionBody, Statement)
TOKEN(ReturnType, ReturnType, Statement)
TOKEN(BodyExpr, BodyExpr, Statement)

TOKEN(EmptyNode, EmptyNode, Statement)
TOKEN(EOS, EOS, Statement)

// ------ expressions ------
EXPR(LiteralExpr, LiteralExpr, Primary)
EXPR(IdentifierExpr, IdentifierExpr, Primary)
EXPR(ParenthesizedExpr, ParenthesizedExpr, Primary)

EXPR(StaticAccessExpr, StaticAccessExpr, StaticAccess)

EXPR(PostIncrementExpr, UnaryOperatorExpr, Postfix)
EXPR(PostDecrementExpr, UnaryOperatorExpr, Postfix)
EXPR(CallExpr, CallExpr, Postfix)
EXPR(IndexExpr, CallExpr, Postfix)
EXPR(Parameterize, Parameterize, Postfix)
EXPR(MemberAccessExpr, AccessExpr, Postfix)

EXPR(LogicalNotExpr, UnaryOperatorExpr, Unary)
EXPR(BitwiseNotExpr, UnaryOperatorExpr, Unary)
EXPR(PreIncrementExpr, UnaryOperatorExpr, Unary)
EXPR(PreDecrementExpr, UnaryOperatorExpr, Unary)
EXPR(PlusExpr, UnaryOperatorExpr, Unary)
EXPR(NegateExpr, UnaryOperatorExpr, Unary)
EXPR(DereferenceExpr, UnaryOperatorExpr, Unary)

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

EXPR(IfExpr, IfExpr, ConditionalIf)
EXPR(CommaElseExpr, CommaElseExpr, ConditionalElse)
EXPR(CallArgument, CallArgument, CallArgument)


// ------ statements ------
STMT(AssignStmt, UpdateStmt)
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
STMT(LogicalAndUpdateStmt, UpdateStmt)
STMT(LogicalOrUpdateStmt, UpdateStmt)

STMT(CompoundStmt, CompoundStmt)
STMT(ExpressionStmt, ExpressionStmt)
STMT(IfStmt, IfStmt)
STMT(ElseStmt, ElseStmt)
STMT(ReturnStmt, ReturnStmt)
STMT(DestroyStmt, DestroyStmt)
STMT(DiscardStmt, DiscardStmt)
STMT(EmptyReturnStmt, EmptyReturnStmt)

// ------ declarations ------
DECL(NamespaceDecl, NamespaceDecl)
DECL(FunctionDecl, FunctionDecl)
DECL(FunctionImplDecl, FunctionDecl)
DECL(StructDecl, StructDecl)
DECL(StructImplDecl, StructDecl)
DECL(EnumDecl, EnumDecl)
DECL(EnumImplDecl, EnumDecl)
DECL(ImplicitEnumValueDecl, EnumValueDecl)
DECL(ExplicitEnumValueDecl, EnumValueDecl)
DECL(MemberDecl, MemberDecl)
DECL(HasMemberDecl, HasMemberDecl)

DECL(VarValueDecl, x)
DECL(LetValueDecl, x)
DECL(UniqueReferenceDecl, x)
DECL(SharedReferenceDecl, x)
DECL(ConstUniqueReferenceDecl, x)
DECL(ConstSharedReferenceDecl, x)
DECL(GenericCategoryVariableDecl, x)

ATTRIBUTE(TemplateAttribute)
ATTRIBUTE(IncompleteAttribute)
ATTRIBUTE(VirtualAttribute)


#undef EXPR
#undef STMT
#undef DECL
#undef ATTRIBUTE
#undef TOKEN