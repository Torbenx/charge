#pragma once

#include <sema/Context.h>
#include <sema/Util.h>

namespace sema {

struct FoldBase;

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
    void formatProgramAsReferencedDeclaration(FoldBase, bool formatAsIncomplete, bool isImpl);
    bool formatAsReferencedDeclaration(Constant);
    void formatDeclaration();

    //! Emits a "template(...)" clause of a program
    void formatTemplateClause(FoldBase);

    Constant fold(const FoldBase&, ExternConstant);
    Constant fold(Constant base, ExternConstant);
    VariableCategory foldCategory(const FoldBase&, VariableCategory);

    std::string output = {};
};

}
