#ifndef INSTRUCTION
#define INSTRUCTION(name, ...)
#endif

INSTRUCTION(Store, MemoryLoc loc, Expr value)
INSTRUCTION(Call, Fn target, ExprList args)
INSTRUCTION(Phi, PhiParentList parents)
INSTRUCTION(Jump, CodePos target)
INSTRUCTION(Branch, Bool cond, CodePos ifTrue, CodePos ifFalse)

#undef INSTRUCTION