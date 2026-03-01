#include <check/MemberExpressions.h>
#include <check/SimpleVariables.h>

namespace check {

MemoryLocation SimpleVariables::declareVariable(Solver& solver, Type type, CodePosition position) {
    uint32_t id = variables.size();
    MemoryLocation location { (uint32_t)theoryId(), id };
    variables.push_back(VariableInfo {
        MemoryDeclarationTheory::DeclarationInfo { type, position },
        solver.memberExpressions().literals().identity(solver, type),
    });
    return location;
}

}