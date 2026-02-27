#ifndef TOKEN
#define TOKEN(kind, lexToken, data1, data2)
#endif

#ifndef DATAKIND
#define DATAKIND(type, name)
#endif

#ifndef MARKER
#define MARKER(name)
#endif

DATAKIND(void, None)
DATAKIND(Word, Word)
DATAKIND(CallArgumentsHandle, CallArguments)
DATAKIND(std::optional<sema::Expression>, Expression)
DATAKIND(sema::VariableKind, VariableKind)
DATAKIND(sema::DeclarationValue, DeclarationValue)
DATAKIND(uint32_t, DeclIndex)

TOKEN(FunctionBody, Colon, None, None)
TOKEN(ReturnType, MinusGreater, None, Expression)
TOKEN(BodyExpr, EqualGreater, None, Expression)

TOKEN(EmptyNode, Invalid, None, None)
TOKEN(EOS, EOS, None, None)

// ------ expressions ------
MARKER(ExpressionsBegin)
TOKEN(LiteralExpr, Literal, None, None)
TOKEN(IdentifierExpr, Identifier, Word, Expression)
TOKEN(ParenthesizedExpr, LeftParen, CallArguments, Expression)
TOKEN(ImplicitSelfReference, Point, None, Expression)

TOKEN(StaticAccessExpr, Identifier, Word, Expression)

TOKEN(PostIncrementExpr, PlusPlus, None, Expression)
TOKEN(PostDecrementExpr, MinusMinus, None, Expression)
TOKEN(CallExpr, LeftParen, CallArguments, Expression)
TOKEN(IndexExpr, LeftSquare, CallArguments, Expression)
TOKEN(Parameterize, LeftBrace, CallArguments, Expression)
TOKEN(MemberAccessExpr, Identifier, Word, Expression)

MARKER(UnaryExprsBegin)
TOKEN(LogicalNotExpr, Exclaim, None, Expression)
TOKEN(BitwiseNotExpr, Tilde, None, Expression)
TOKEN(PreIncrementExpr, PlusPlus, None, Expression)
TOKEN(PreDecrementExpr, MinusMinus, None, Expression)
TOKEN(PlusExpr, Plus, None, Expression)
TOKEN(NegateExpr, Minus, None, Expression)
TOKEN(DereferenceExpr, Star, None, Expression)
MARKER(UnaryExprsEnd)

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

TOKEN(CompareNotEqualExpr, ExclaimEqual, None, Expression)
TOKEN(CompareEqualExpr, EqualEqual, None, Expression)
TOKEN(CompareLessExpr, Less, None, None)
TOKEN(CompareLessEqualExpr, LessEqual, None, Expression)
TOKEN(CompareGreaterExpr, Greater, None, None)
TOKEN(CompareGreaterEqualExpr, GreaterEqual, None, Expression)

TOKEN(IfExpr, EqualGreater, None, Expression)
TOKEN(CommaElseExpr, EqualGreater, None, Expression)
TOKEN(CallArgument, Invalid, None, Expression)
MARKER(ExpressionsEnd)


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
TOKEN(NamespaceDecl, Identifier, Word, DeclarationValue)
MARKER(ProgramDeclsBegin)
TOKEN(FunctionDecl, Identifier, Word, None)
TOKEN(FunctionImplDecl, Impl, None, None)
TOKEN(StructDecl, Identifier, Word, None)
TOKEN(StructImplDecl, Impl, None, None)
TOKEN(EnumDecl, Identifier, Word, None)
TOKEN(EnumImplDecl, Impl, None, None)
TOKEN(GlobalDecl, Identifier, Word, None)
TOKEN(GlobalImplDecl, Impl, None, None)
MARKER(ProgramDeclsEnd)

MARKER(EnumValueDeclsBegin)
TOKEN(ImplicitEnumValueDecl, Identifier, Word, DeclIndex)
TOKEN(ExplicitEnumValueDecl, Identifier, Word, DeclIndex)
MARKER(EnumValueDeclsEnd)

MARKER(MemberDeclsBegin)
TOKEN(MemberDecl, Identifier, Word, DeclIndex)
TOKEN(HasMemberDecl, Has, DeclIndex, Expression)
MARKER(MemberDeclsEnd)

MARKER(VariableDeclsBegin)
TOKEN(VarValueDecl, Identifier, Word, Expression)
TOKEN(LetValueDecl, Identifier, Word, Expression)
MARKER(VariableDeclsEnd)

TOKEN(VariableType, Colon, VariableKind, Expression)
TOKEN(VariableGenericCategory, Less, None, Expression)

TOKEN(TemplateAttribute, Template, None, None)
TOKEN(IncompleteAttribute, Incomplete, None, None)
TOKEN(VirtualAttribute, Virtual, None, None)

#undef MARKER
#undef TOKEN
#undef DATAKIND