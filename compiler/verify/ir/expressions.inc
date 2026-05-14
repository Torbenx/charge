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

INLINE_EXPR(FalseLiteral, Bool, bool)
COMPOUND_EXPR(BoolLoad, Bool, MemoryLoc loc, CodePos pos)
COMPOUND_EXPR(BoolReturn, Bool, CodePos call, uint32_t index)
INLINE_EXPR(PhiParentEdgeTaken, Bool, PhiParent)

COMPOUND_EXPR(TypeLiteral, Type, TypeDecl decl, ExprList args)
COMPOUND_EXPR(TypeLoad, Type, MemoryLoc loc, CodePos pos)
COMPOUND_EXPR(TypeReturn, Type, CodePos call, uint32_t index)
COMPOUND_EXPR(TypeSelfMember, Member, Type type)
COMPOUND_EXPR(TypeIsImpl, Bool, Type type, TypeImpl impl)

INLINE_EXPR(MemberLiteral, Member, MemberLiteral)
COMPOUND_EXPR(MemberLoad, Member, MemoryLoc loc, CodePos pos)
COMPOUND_EXPR(MemberReturn, Member, CodePos call, uint32_t index)
COMPOUND_EXPR(MemberCompose, Member, Member left, Member right)
COMPOUND_EXPR(MemberContains, Bool, Member parent, Member child)

// TODO: INLINE_EXPR(MemoryDeclLiteral, MemoryDecl)
COMPOUND_EXPR(MemoryDeclType, Type, MemoryDecl decl)

COMPOUND_EXPR(MakeMemoryLoc, MemoryLoc, MemoryDecl decl, Member member)
COMPOUND_EXPR(MemoryLocLoad, MemoryLoc, MemoryLoc loc, CodePos pos)
COMPOUND_EXPR(MemoryLocReturn, MemoryLoc, CodePos call, uint32_t index)
COMPOUND_EXPR(MemoryLocDecl, MemoryDecl, MemoryLoc loc)
COMPOUND_EXPR(MemoryLocMember, Member, MemoryLoc loc)

#undef COMPOUND_EXPR
#undef INLINE_EXPR
#undef EXPR