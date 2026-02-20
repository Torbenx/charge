#ifndef TOKEN
#define TOKEN(kind, lexToken, data1, data2)
#endif

#ifndef DATAKIND
#define DATAKIND(type, name)
#endif

DATAKIND(void, None)
DATAKIND(Word, Word)
DATAKIND(sema::DeclarationValue, DeclarationValue)
DATAKIND(CallArgumentsHandle, CallArguments)
DATAKIND(sema::Expression, Expression)
DATAKIND(sema::VariableKind, VariableKind)

TOKEN(FunctionBody, Colon, None, None)
TOKEN(ReturnType, MinusGreater, None, Expression)
TOKEN(BodyExpr, EqualGreater, None, Expression)

TOKEN(EmptyNode, Invalid, None, None)
TOKEN(EOS, EOS, None, None)

// ------ expressions ------
TOKEN(LiteralExpr, Literal, None, None)
TOKEN(IdentifierExpr, Identifier, Word, Expression)
TOKEN(ParenthesizedExpr, LeftParen, CallArguments, Expression)
TOKEN(ImplicitSelfReference, Point, None, Expression)

TOKEN(StaticAccessExpr, Identifier, Word, Expression)

TOKEN(PostIncrementExpr, PlusPlus, None, None)
TOKEN(PostDecrementExpr, MinusMinus, None, None)
TOKEN(CallExpr, LeftParen, CallArguments, Expression)
TOKEN(IndexExpr, LeftSquare, CallArguments, Expression)
TOKEN(Parameterize, LeftBrace, CallArguments, Expression)
TOKEN(MemberAccessExpr, Identifier, Word, Expression)

TOKEN(LogicalNotExpr, Exclaim, None, Expression)
TOKEN(BitwiseNotExpr, Tilde, None, Expression)
TOKEN(PreIncrementExpr, PlusPlus, None, Expression)
TOKEN(PreDecrementExpr, MinusMinus, None, Expression)
TOKEN(PlusExpr, Plus, None, Expression)
TOKEN(NegateExpr, Minus, None, Expression)
TOKEN(DereferenceExpr, Star, None, Expression)

TOKEN(AdditionExpr, Plus, None, Expression)
TOKEN(SubtractionExpr, Minus, None, Expression)
TOKEN(MultiplyExpr, Star, None, Expression)
TOKEN(BitwiseAndExpr, Amp, None, Expression)
TOKEN(BitwiseXorExpr, Hat, None, Expression)
TOKEN(BitwiseOrExpr, Vert, None, Expression)
TOKEN(DivideExpr, Slash, None, Expression)
TOKEN(RemainderExpr, Percent, None, Expression)
TOKEN(ShiftLeftExpr, LessLess, None, Expression)
TOKEN(ShiftRightExpr, GreaterGreater, None, Expression)
TOKEN(LogicalAndExpr, AmpAmp, None, Expression)
TOKEN(LogicalOrExpr, VertVert, None, Expression)

TOKEN(CompareNotEqualExpr, ExclaimEqual, None, None)
TOKEN(CompareEqualExpr, EqualEqual, None, None)
TOKEN(CompareLessExpr, Less, None, None)
TOKEN(CompareLessEqualExpr, LessEqual, None, None)
TOKEN(CompareGreaterExpr, Greater, None, None)
TOKEN(CompareGreaterEqualExpr, GreaterEqual, None, None)

TOKEN(IfExpr, EqualGreater, None, None)
TOKEN(CommaElseExpr, EqualGreater, None, None)
TOKEN(CallArgument, Invalid, None, Expression)


// ------ statements ------
TOKEN(AssignStmt, Equal, None, Expression)
TOKEN(AdditionUpdateStmt, PlusEqual, None, None)
TOKEN(SubtractionUpdateStmt, MinusEqual, None, None)
TOKEN(MultiplyUpdateStmt, StarEqual, None, None)
TOKEN(BitwiseAndUpdateStmt, AmpEqual, None, None)
TOKEN(BitwiseXorUpdateStmt, HatEqual, None, None)
TOKEN(BitwiseOrUpdateStmt, VertEqual, None, None)
TOKEN(DivideUpdateStmt, SlashEqual, None, None)
TOKEN(RemainderUpdateStmt, PercentEqual, None, None)
TOKEN(ShiftLeftUpdateStmt, LessLessEqual, None, None)
TOKEN(ShiftRightUpdateStmt, GreaterGreaterEqual, None, None)
TOKEN(LogicalAndUpdateStmt, AmpAmpEqual, None, None)
TOKEN(LogicalOrUpdateStmt, VertVertEqual, None, None)

TOKEN(CompoundStmt, LeftBrace, None, None)
TOKEN(ExpressionStmt, Invalid, None, None)
TOKEN(IfStmt, Colon, None, None)
TOKEN(ElseStmt, Colon, None, None)
TOKEN(ReturnStmt, Return, None, None)
TOKEN(DestroyStmt, Destroy, None, None)
TOKEN(DiscardStmt, Discard, None, None)
TOKEN(EmptyReturnStmt, Return, None, None)

// ------ declarations ------
TOKEN(NamespaceDecl, Identifier, DeclarationValue, None)
TOKEN(FunctionDecl, Identifier, DeclarationValue, None)
TOKEN(FunctionImplDecl, Impl, DeclarationValue, None)
TOKEN(StructDecl, Identifier, DeclarationValue, None)
TOKEN(StructImplDecl, Impl, DeclarationValue, None)
TOKEN(EnumDecl, Identifier, DeclarationValue, None)
TOKEN(EnumImplDecl, Impl, DeclarationValue, None)
TOKEN(ImplicitEnumValueDecl, Identifier, DeclarationValue, None)
TOKEN(ExplicitEnumValueDecl, Identifier, DeclarationValue, None)
TOKEN(MemberDecl, Identifier, DeclarationValue, None)
TOKEN(HasMemberDecl, Has, DeclarationValue, Expression)
TOKEN(GlobalDecl, Identifier, DeclarationValue, None)
TOKEN(GlobalImplDecl, Impl, DeclarationValue, None)

TOKEN(VarValueDecl, Identifier, Word, None)
TOKEN(LetValueDecl, Identifier, Word, None)
TOKEN(VariableType, Colon, VariableKind, Expression)
TOKEN(VariableGenericCategory, Less, None, Expression)

TOKEN(TemplateAttribute, Template, None, None)
TOKEN(IncompleteAttribute, Incomplete, None, None)
TOKEN(VirtualAttribute, Virtual, None, None)

#undef TOKEN
#undef DATAKIND