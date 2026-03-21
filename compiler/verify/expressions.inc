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
INLINE_EXPR(PhiParent, Bool, PhiParent)

COMPOUND_EXPR(TypeLiteral, Type, TypeDecl decl, ExprList args)
COMPOUND_EXPR(TypeLoad, Type, MemoryLoc loc, CodePos pos)
COMPOUND_EXPR(TypeSelfMember, Member, Type type)

INLINE_EXPR(MemberLiteral, Member, MemberLiteral)
COMPOUND_EXPR(MemberLoad, Member, MemoryLoc loc, CodePos pos)
COMPOUND_EXPR(MemberCompose, Member, Member left, Member right)
COMPOUND_EXPR(MemberContains, Bool, Member parent, Member child)

// TODO: INLINE_EXPR(MemoryDeclLiteral, MemoryDecl)
COMPOUND_EXPR(MemoryDeclType, Type, MemoryDecl decl)

COMPOUND_EXPR(MakeMemoryLoc, MemoryLoc, MemoryDecl decl, Member member)
COMPOUND_EXPR(MemoryLocLoad, MemoryLoc, MemoryLoc loc, CodePos pos)

#undef COMPOUND_EXPR
#undef INLINE_EXPR
#undef EXPR