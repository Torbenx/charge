#pragma once

#include <sema/Program.h>

namespace sema {

struct Util {
    Util(Context& context, ProgramHandle handle);

    Program* get(ProgramHandle);
    Namespace* get(NamespaceHandle);

    std::strong_ordering compare(Value, Value);
    std::strong_ordering compare(ProgramHandle, ProgramHandle);
    std::strong_ordering compare(NamespaceHandle, NamespaceHandle);
    std::strong_ordering compare(Parameterize, Parameterize);
    std::strong_ordering compare(MemberPointer, MemberPointer);
    std::strong_ordering compare(RemoteExpression, RemoteExpression);

    Context& context;
    Program* program;
    ProgramHandle programHandle;
};

}