#ifndef EXPR_KIND
#define EXPR_KIND(name, signature)
#endif

EXPR_KIND(BoolLiteral, Bool())
EXPR_KIND(BoolLoad, Bool(MemoryDecl, Member, CodePos))
EXPR_KIND(BoolEquality, Bool(Bool, Bool))

EXPR_KIND(TypeLiteral, Type(ExprVector))
EXPR_KIND(TypeLoad, Type(MemoryDecl, Member, CodePos))
EXPR_KIND(TypeEquality, Bool(Type, Type))
EXPR_KIND(TypeMemberTree, MemberTree(Type))

EXPR_KIND(MemberTreeLiteral, MemberTree)
EXPR_KIND(MemberTreeEquality, Bool(MemberTree, MemberTree))

EXPR_KIND(MemberLiteral, Member())
EXPR_KIND(MemberLoad, Member(MemoryDecl, Member, CodePos))
EXPR_KIND(MemberEquality, Bool(Member, Member))
EXPR_KIND(MemberComposite, Member(Member, Member))
EXPR_KIND(MemberContains, Bool(Member, Member))
EXPR_KIND(MemberTreeFor, MemberTree(Member))

EXPR_KIND(MemoryDeclLiteral, MemoryDecl())
EXPR_KIND(MemoryDeclEquality, Bool(MemoryDecl, MemoryDecl))

EXPR_KIND(LocationLoadDecl, MemoryDecl(MemoryDecl, Member, CodePos))
EXPR_KIND(LocationLoadMember, Member(MemoryDecl, Member, CodePos))

#undef EXPR_KIND