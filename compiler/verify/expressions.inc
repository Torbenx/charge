#ifndef EXPR
#define EXPR(name, sort, ...)
#endif
#ifndef SIMPLE_EXPR
#define SIMPLE_EXPR(name, sort) EXPR(name, sort)
#endif

EXPR(Equality, Bool, Expr left, Expr right)

SIMPLE_EXPR(FalseLiteral, Bool)
EXPR(BoolLoad, Bool, MemoryLoc loc, CodePos pos)

EXPR(TypeLiteral, Type, TypeDecl decl, ExprList args)
EXPR(TypeLoad, Type, MemoryLoc loc, CodePos pos)
EXPR(TypeSelfMember, Member, Type type)

SIMPLE_EXPR(MemberLiteral, Member)
EXPR(MemberLoad, Member, MemoryLoc loc, CodePos pos)
EXPR(MemberCompose, Member, Member left, Member right)
EXPR(MemberContains, Bool, Member parent, Member child)

SIMPLE_EXPR(MemoryDeclLiteral, MemoryDecl)
EXPR(MemoryDeclType, Type, MemoryDecl decl)

EXPR(MemoryLocLoad, MemoryLoc, MemoryLoc loc, CodePos pos)

#undef SIMPLE_EXPR
#undef EXPR