#ifndef ANY_DECL
#define ANY_DECL(kind, type)
#endif

#ifndef TREE_DECL
#define TREE_DECL(kind, type) ANY_DECL(kind, type)
#endif

#ifndef EXTRA_DECL
#define EXTRA_DECL(kind, type) ANY_DECL(kind, type)
#endif

TREE_DECL(Namespace, NamespaceDecl)
TREE_DECL(StructType, TypeDecl)
TREE_DECL(ObjectType, TypeDecl)
TREE_DECL(Function, FunctionDecl)
TREE_DECL(StaticLetVariable, StaticVariableDecl)
TREE_DECL(StaticVarVariable, StaticVariableDecl)
TREE_DECL(Member, ParameterOrMemberDecl)
TREE_DECL(HasMember, HasMemberDecl)
TREE_DECL(InParameter, ParameterOrMemberDecl)
TREE_DECL(VarParameter, ParameterOrMemberDecl)
TREE_DECL(InOutParameter, ParameterOrMemberDecl)
TREE_DECL(OutParameter, ParameterOrMemberDecl)
TREE_DECL(BlockLetVariable, BlockVariableDecl)
TREE_DECL(BlockVarVariable, BlockVariableDecl)

EXTRA_DECL(Block, BlockDecl)

#undef EXTRA_DECL
#undef TREE_DECL
#undef ANY_DECL