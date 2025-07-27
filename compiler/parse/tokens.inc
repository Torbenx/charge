#ifndef TOKEN
#define TOKEN(kind, data1, data2)
#endif

#ifndef DATAKIND
#define DATAKIND(type, name)
#endif

DATAKIND(void, None)
DATAKIND(Word, Word)
DATAKIND(sema::DeclarationValue, DeclarationValue)
DATAKIND(CallArgumentsHandle, CallArguments)

TOKEN(FunctionBody, None, None)
TOKEN(ReturnType, None, None)
TOKEN(BodyExpr, None, None)

TOKEN(EmptyNode, None, None)
TOKEN(EOS, None, None)

// ------ expressions ------
TOKEN(LiteralExpr, None, None)
TOKEN(IdentifierExpr, Word, None)
TOKEN(ParenthesizedExpr, CallArguments, None)

TOKEN(StaticAccessExpr, Word, None)

TOKEN(PostIncrementExpr, None, None)
TOKEN(PostDecrementExpr, None, None)
TOKEN(CallExpr, CallArguments, None)
TOKEN(IndexExpr, CallArguments, None)
TOKEN(Parameterize, CallArguments, None)
TOKEN(MemberAccessExpr, Word, None)

TOKEN(LogicalNotExpr, None, None)
TOKEN(BitwiseNotExpr, None, None)
TOKEN(PreIncrementExpr, None, None)
TOKEN(PreDecrementExpr, None, None)
TOKEN(PlusExpr, None, None)
TOKEN(NegateExpr, None, None)
TOKEN(DereferenceExpr, None, None)

TOKEN(AdditionExpr, None, None)
TOKEN(SubtractionExpr, None, None)
TOKEN(MultiplyExpr, None, None)
TOKEN(BitwiseAndExpr, None, None)
TOKEN(BitwiseXorExpr, None, None)
TOKEN(BitwiseOrExpr, None, None)
TOKEN(DivideExpr, None, None)
TOKEN(RemainderExpr, None, None)
TOKEN(ShiftLeftExpr, None, None)
TOKEN(ShiftRightExpr, None, None)
TOKEN(LogicalAndExpr, None, None)
TOKEN(LogicalOrExpr, None, None)

TOKEN(CompareNotEqualExpr, None, None)
TOKEN(CompareEqualExpr, None, None)
TOKEN(CompareLessExpr, None, None)
TOKEN(CompareLessEqualExpr, None, None)
TOKEN(CompareGreaterExpr, None, None)
TOKEN(CompareGreaterEqualExpr, None, None)

TOKEN(IfExpr, None, None)
TOKEN(CommaElseExpr, None, None)
TOKEN(CallArgument, Word, None)


// ------ statements ------
TOKEN(AssignStmt, None, None)
TOKEN(AdditionUpdateStmt, None, None)
TOKEN(SubtractionUpdateStmt, None, None)
TOKEN(MultiplyUpdateStmt, None, None)
TOKEN(BitwiseAndUpdateStmt, None, None)
TOKEN(BitwiseXorUpdateStmt, None, None)
TOKEN(BitwiseOrUpdateStmt, None, None)
TOKEN(DivideUpdateStmt, None, None)
TOKEN(RemainderUpdateStmt, None, None)
TOKEN(ShiftLeftUpdateStmt, None, None)
TOKEN(ShiftRightUpdateStmt, None, None)
TOKEN(LogicalAndUpdateStmt, None, None)
TOKEN(LogicalOrUpdateStmt, None, None)

TOKEN(CompoundStmt, None, None)
TOKEN(ExpressionStmt, None, None)
TOKEN(IfStmt, None, None)
TOKEN(ElseStmt, None, None)
TOKEN(ReturnStmt, None, None)
TOKEN(DestroyStmt, None, None)
TOKEN(DiscardStmt, None, None)
TOKEN(EmptyReturnStmt, None, None)

// ------ declarations ------
TOKEN(NamespaceDecl, DeclarationValue, None)
TOKEN(FunctionDecl, DeclarationValue, None)
TOKEN(FunctionImplDecl, DeclarationValue, None)
TOKEN(StructDecl, DeclarationValue, None)
TOKEN(StructImplDecl, DeclarationValue, None)
TOKEN(EnumDecl, DeclarationValue, None)
TOKEN(EnumImplDecl, DeclarationValue, None)
TOKEN(ImplicitEnumValueDecl, DeclarationValue, None)
TOKEN(ExplicitEnumValueDecl, DeclarationValue, None)
TOKEN(MemberDecl, DeclarationValue, None)
TOKEN(HasMemberDecl, DeclarationValue, None)
TOKEN(GlobalDecl, DeclarationValue, None)

TOKEN(VarValueDecl, Word, None)
TOKEN(LetValueDecl, Word, None)
TOKEN(UniqueReferenceDecl, Word, None)
TOKEN(SharedReferenceDecl, Word, None)
TOKEN(ConstUniqueReferenceDecl, Word, None)
TOKEN(ConstSharedReferenceDecl, Word, None)
TOKEN(GenericCategoryVariableDecl, Word, None)

TOKEN(TemplateAttribute, None, None)
TOKEN(IncompleteAttribute, None, None)
TOKEN(VirtualAttribute, None, None)

#undef TOKEN
#undef DATAKIND