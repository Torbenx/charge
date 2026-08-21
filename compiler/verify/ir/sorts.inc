// The name of a sort is also the name of the expression type representing it, so it can be
// used to generate both. The snake_case spelling is the one used in the text form of the IR.
#ifndef SORT
#define SORT(name, snake_case)
#endif

#ifndef SET_SORT
#define SET_SORT(name, snake_case) SORT(name, snake_case)
#endif

SORT(Bool, bool)
SORT(Fn, fn)
SORT(Type, type)
SORT(Member, member)
SORT(MemoryDecl, memory_decl)
SORT(MemoryLoc, memory_loc)
SET_SORT(MemorySet, memory_set)
SET_SORT(InvariantSet, invariant_set)

SORT(UninterpretedConstant, uninterpreted_constant)

#undef SET_SORT
#undef SORT
