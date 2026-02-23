#pragma once

#include <sema/Context.h>
#include <sema/Util.h>

namespace sema {

struct Formatter : Util {
    using Util::Util;
    Formatter(const Util& util)
        : Util(util) { }
    void formatWord(Word);
    void formatNamespace(NamespaceHandle);
    void formatNamespaceQualifier(NamespaceHandle);
    void formatProgramBare(ProgramHandle, std::span<const Constant>);
    void formatCompleteProgram(ProgramHandle, std::span<const Constant>);
    void formatConstant(Constant);
    void formatVariableDeclaration(Word name, Constant type, VariableCategory category);
    void formatEnumValueDeclaration(Constant enumType, int_t valueIndex);
    void formatMemberDeclaration(Constant structType, int_t memberIndex);
    bool formatAsDeclaration(Constant);

    //! Change formatting context
    /*!
    TODO: This is workaround to not having access to a scratch program,
          which would allow folding values instead.
    */
    template<typename F>
    void formatAs(ProgramHandle handle, F&& f) {
        ProgramHandle curProgramHandle = programHandle;
        Program* curProgram = program;
        programHandle = handle;
        program = context.program(handle);
        f();
        programHandle = curProgramHandle;
        program = curProgram;
    }

    std::string output = {};
};

}