#pragma once

#include <sema/Program.h>

namespace sema {

struct FoldBase {
    Program* program;
    ModuleHandle module;
    ProgramHandle programHandle;
    Constant value;
    std::span<const Constant> arguments;
};

struct Util {
    Util(Context& context, ProgramHandle handle);

    std::optional<ProgramHandle> baseProgram(Constant value) {
        return program->baseProgram(value);
    }

    std::strong_ordering compare(Constant, Constant);
    std::strong_ordering compare(ProgramHandle, ProgramHandle);
    std::strong_ordering compare(NamespaceHandle, NamespaceHandle);
    std::strong_ordering compare(Parameterize, Parameterize);
    std::strong_ordering compare(MemberPointer, MemberPointer);
    std::strong_ordering compare(RemoteComputation, RemoteComputation);
    std::strong_ordering compare(EnumValue, EnumValue);

    FoldBase asFoldBase(Constant value);
    std::optional<FoldBase> tryAsFoldBase(Constant value);

    Context& context;
    Program* program;
    ProgramHandle programHandle;
};

}