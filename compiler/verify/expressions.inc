#ifndef EXPR
#define EXPR(name, result, ...)
#endif
#ifndef SIMPLE_EXPR
#define SIMPLE_EXPR(name, result) EXPR(name, result)
#endif

EXPR(Equality, Bool, Expr left, Expr right)

SIMPLE_EXPR(FalseLiteral, Bool)
EXPR(BoolLoad, Bool, MemoryDecl decl, Member member, CodePos pos)

EXPR(TypeLiteral, Type, TypeDecl decl, ExprList args)
EXPR(TypeLoad, Type, MemoryDecl decl, Member member, CodePos pos)
EXPR(TypeSelfMember, Member, Type type)

SIMPLE_EXPR(MemberLiteral, Member)
EXPR(MemberLoad, Member, MemoryDecl decl, Member member, CodePos pos)
EXPR(MemberCompose, Member, Member left, Member right)
EXPR(MemberContains, Bool, Member parent, Member child)

SIMPLE_EXPR(MemoryDeclLiteral, MemoryDecl)
EXPR(MemoryDeclType, Type, MemoryDecl decl)

EXPR(LocationLoadDecl, MemoryDecl, MemoryDecl decl, Member member, CodePos pos)
EXPR(LocationLoadMember, Member, MemoryDecl decl, Member member, CodePos pos)

#undef SIMPLE_EXPR
#undef EXPR