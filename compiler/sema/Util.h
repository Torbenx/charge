#pragma once

#include <sema/Program.h>

namespace sema {

struct Util {
    Util(Context& context, ProgramHandle handle);

    Program* get(ProgramHandle);
    Namespace* get(NamespaceHandle);

    std::strong_ordering compare(Constant, Constant);
    std::strong_ordering compare(ProgramHandle, ProgramHandle);
    std::strong_ordering compare(NamespaceHandle, NamespaceHandle);
    std::strong_ordering compare(Parameterize, Parameterize);
    std::strong_ordering compare(MemberPointer, MemberPointer);
    std::strong_ordering compare(RemoteComputation, RemoteComputation);
    std::strong_ordering compare(EnumValue, EnumValue);

    Context& context;
    Program* program;
    ProgramHandle programHandle;
};

}