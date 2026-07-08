#ifndef EXPR
#define EXPR(name, sort)
#endif
#ifndef COMPOUND_EXPR
#define COMPOUND_EXPR(name, sort, ...) EXPR(name, sort)
#endif
#ifndef INLINE_EXPR
#define INLINE_EXPR(name, sort, arg) EXPR(name, sort)
#endif

COMPOUND_EXPR(Equality, Bool, Expr left, Expr right)
COMPOUND_EXPR(Load, Expr, MemoryLoc loc, RelativeCodePos pos)
COMPOUND_EXPR(PureScalarReturn, Expr, Fn target, ExprList args)
INLINE_EXPR(FunctionParameter, Expr, uint32_t parameterIndex)

INLINE_EXPR(BooleanLiteral, Bool, bool)
INLINE_EXPR(PositionActive, Bool, CodePos)
INLINE_EXPR(ParentEdgeTaken, Bool, PhiParent)

COMPOUND_EXPR(TypeIsImpl, Bool, Type type, TypeImpl impl)

INLINE_EXPR(MemberLiteral, Member, MemberLiteral)
COMPOUND_EXPR(TypeSelfMember, Member, Type type)
COMPOUND_EXPR(MemberCompose, Member, Member left, Member right)
COMPOUND_EXPR(MemberContains, Bool, Member parent, Member child)

// TODO: INLINE_EXPR(MemoryDeclLiteral, MemoryDecl)
COMPOUND_EXPR(MemoryDeclType, Type, MemoryDecl decl)

COMPOUND_EXPR(MakeMemoryLoc, MemoryLoc, MemoryDecl decl, Member member)
COMPOUND_EXPR(MemoryLocType, Type, MemoryLoc loc)
COMPOUND_EXPR(MemoryLocDecl, MemoryDecl, MemoryLoc loc)
COMPOUND_EXPR(MemoryLocMember, Member, MemoryLoc loc)

#undef COMPOUND_EXPR
#undef INLINE_EXPR
#undef EXPR