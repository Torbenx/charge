#include <sema/Formatter.h>

#include <sema/Context.h>

namespace sema {

void Formatter::formatWord(Word word) {
    output += context.tokenBuffer.wordTable.view(word);
}

void Formatter::formatNamespaceQualifier(NamespaceHandle nsHandle) {
    if (nsHandle == context.globalNamespace())
        return;
    auto* ns = context.getNamespace(nsHandle);
    formatNamespaceQualifier(ns->parent.value());
    formatWord(ns->name);
    output += "::";
}

void Formatter::formatNamespace(NamespaceHandle nsHandle) {
    if (nsHandle == context.globalNamespace()) {
        output += "global_namespace";
        return;
    }
    auto* ns = context.getNamespace(nsHandle);
    formatNamespaceQualifier(ns->parent.value());
    formatWord(ns->name);
}

void Formatter::formatProgramBare(ProgramHandle progHandle, std::span<const Constant> arguments) {
    auto* prog = context.program(progHandle);
    auto parent = prog->parent();
    if (parent.kind() == DeclarationValueKind::Namespace) {
        formatNamespaceQualifier(parent.nsHandle());
    } else if (parent.kind() == DeclarationValueKind::Program) {
        formatCompleteProgram(parent.program(), arguments.subspan(0, prog->inheritedParameterCount));
        output += "::";
    }
    VERIFY(!prog->isImpl());
    formatWord(prog->name());
}

void Formatter::formatCompleteProgram(ProgramHandle progHandle, std::span<const Constant> arguments) {
    formatProgramBare(progHandle, arguments);

    auto* prog = context.program(progHandle);
    VERIFY(prog->parameters.size() == arguments.size());
    if (!prog->isTemplate())
        return;
    output += "{";
    bool first = true;
    for (int_t i = prog->inheritedParameterCount; i < (int_t)arguments.size(); i++) {
        if (prog->parameters[i].implicit())
            continue;
        if (first)
            first = false;
        else
            output += ", ";
        Word name = prog->parameters[i].name;
        if (!name.empty()) {
            formatWord(name);
            output += ": ";
        }
        formatConstant(arguments[i]);
    }
    output += "}";
}

void Formatter::formatConstant(Constant c) {
    if (c.isEnumValueLiteral()) {
        auto value = program->getEnumValue(c);
        formatConstant(Constant(value.enumType));
        output += "::";
        auto* enumProg = cast<EnumProgram>(context.program(baseProgram(Constant(value.enumType)).value()));
        formatWord(enumProg->values[value.valueIndex].name());
        return;
    }

    switch (c.kind()) {
    case ConstantKind::Computed:
    case ConstantKind::RemoteComputed:
    case ConstantKind::TemplateSignature$Program:
    case ConstantKind::TemplateSignature$Parameterize:
    case ConstantKind::FunctionSignature$Program:
    case ConstantKind::FunctionSignature$Parameterize:
    case ConstantKind::CopyOfParameterToReferenceCategory:
        output += "...";
        return;
    case ConstantKind::Namespace:
        formatNamespace(c.nsHandle());
        return;
    case ConstantKind::MemberPointer: {
        MemberPointer pointer = program->getMemberPointer(c);
        if (pointer.isIdentity()) {
            formatConstant(pointer.memberType);
            output += "::self";
        } else {
            formatConstant(pointer.elements.front().structImpl);
            for (auto elem : pointer.elements) {
                auto progHandle = baseProgram(elem.structImpl).value();
                auto* structProg = cast<StructProgram>(context.program(progHandle));
                const auto& member = structProg->members[elem.memberIndex];
                output += "::";
                if (member.isBase()) {
                    output += "base ";
                    formatAs(progHandle, [&] { formatConstant(Constant(member.type())); });
                } else {
                    formatWord(member.name());
                }
            }
        }
        return;
    }
    case ConstantKind::CopyOfParameter: {
        const auto& p = program->parameters[c.parameterIndex()];
        if (p.implicit())
            output += "_";
        else
            formatWord(p.name);
        return;
    }
    case ConstantKind::CopyOfOpenGlobal$Program:
    case ConstantKind::CopyOfOpenGlobal$Parameterize:
        formatConstant(c.copiedGlobal());
        return;
    case ConstantKind::OpenReturnType$Self:
        output += "return_type";
        return;
    case ConstantKind::OpenReturnType$Parameterize:
        formatConstant(c.returnTypeOf());
        output += "::return_type";
        return;
    case ConstantKind::Parameterize: {
        auto para = program->getParameterize(c);
        Program* baseProg = context.program(para.base);
        if (para.arguments.size() == baseProg->parameters.size())
            formatCompleteProgram(para.base, para.arguments);
        else
            formatProgramBare(para.base, para.arguments);
        return;
    }
    case ConstantKind::Program: {
        formatProgramBare(c.program(), {});
        return;
    }
    case ConstantKind::CopyOfError: {
        output += "error_value";
        return;
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

void Formatter::formatVariableDeclaration(Word name, Constant type, VariableCategory category) {
    if (category.kind() == VariableKind::Let) {
        output += "let ";
    } else if (category.kind() == VariableKind::Var) {
        output += "var ";
    }
    formatWord(name);
    output += ": ";
    if (category.isGeneric()) {
        output += "<";
        formatConstant(category.genericCategory());
        output += "> ";
    } else if (category.kind() == VariableKind::UniqueReference) {
        output += "unique ";
    } else if (category.kind() == VariableKind::ConstUniqueReference) {
        output += "const unique ";
    } else if (category.kind() == VariableKind::SharedReference) {
        output += "shared ";
    } else if (category.kind() == VariableKind::ConstSharedReference) {
        output += "const shared ";
    }
    formatConstant(type);
}

void Formatter::formatEnumValueDeclaration(Constant enumType, int_t valueIndex) {
    ProgramHandle progHandle = baseProgram(enumType).value();
    auto* enumProg = cast<EnumProgram>(context.program(progHandle));
    const auto& valueDecl = enumProg->values[valueIndex];
    formatConstant(enumType);
    output += "::";
    formatWord(valueDecl.name());
    output += " = ";
    formatAs(progHandle, [&] {
        if (valueDecl.explicitValue().has_value()) {
            formatConstant(Constant(valueDecl.explicitValue().value()));
        } else {
            // TODO: Should be able to access the implicit value here
            output += "...";
        }
    });
}

void Formatter::formatMemberDeclaration(Constant structType, int_t memberIndex) {
    ProgramHandle progHandle = baseProgram(structType).value();
    auto* structProg = cast<StructProgram>(context.program(progHandle));
    const auto& member = structProg->members[memberIndex];
    formatConstant(structType);
    output += "::";
    formatAs(progHandle, [&] {
        if (member.isBase()) {
            output += "base ";
            formatConstant(Constant(member.type()));
        } else {
            formatWord(member.name());
            output += ": ";
            formatConstant(Constant(member.type()));
        }
    });
}

bool Formatter::formatAsDeclaration(Constant c) {
    if (c.isEnumValueLiteral()) {
        auto valueRef = program->getEnumValue(c);
        formatEnumValueDeclaration(Constant(valueRef.enumType), valueRef.valueIndex);
        return true;
    }

    switch (c.kind()) {
    case ConstantKind::Namespace:
        if (c.nsHandle() != context.globalNamespace()) {
            output += "namespace ";
            formatNamespace(c.nsHandle());
            return true;
        }
        break;
    case ConstantKind::MemberPointer: {
        formatConstant(c);
        MemberPointer pointer = program->getMemberPointer(c);
        if (!pointer.isIdentity()) {
            const auto lastLink = pointer.elements.back();
            auto* structProg = cast<StructProgram>(context.program(baseProgram(lastLink.structImpl).value()));
            if (!structProg->members[lastLink.memberIndex].isBase()) {
                output += ": ";
                formatConstant(pointer.memberType);
            }
        }
        return true;
    }
    case ConstantKind::CopyOfParameter: {
        const auto& p = program->parameters[c.parameterIndex()];
        output += "let ";
        formatWord(p.name);
        output += ": ";
        formatConstant(Constant(p.type));
        return true;
    }
    case ConstantKind::CopyOfOpenGlobal$Program:
    case ConstantKind::CopyOfOpenGlobal$Parameterize:
        return formatAsDeclaration(c.copiedGlobal());
    case ConstantKind::Program:
    case ConstantKind::Parameterize: {
        ProgramHandle progHandle = baseProgram(c).value();
        Program* prog = context.program(progHandle);
        VERIFY(!prog->isImpl());
        std::span<const Constant> arguments;
        if (c.kind() == ConstantKind::Parameterize)
            arguments = program->getParameterize(c).arguments;

        switch (prog->kind()) {
        case ProgramKind::Struct:
            output += "struct ";
            formatConstant(c);
            break;
        case ProgramKind::Enum:
            output += "enum ";
            formatConstant(c);
            break;
        case ProgramKind::Function: {
            auto* fnProg = cast<FunctionProgram>(prog);
            output += "fn ";
            formatConstant(c);
            formatAs(progHandle, [&] {
                output += "(";
                bool first = true;
                for (const auto& p : fnProg->functionParameters) {
                    if (first)
                        first = false;
                    else
                        output += ", ";
                    formatVariableDeclaration(p.name(), p.type(), p.category());
                }
                output += ") -> ";
                formatConstant(Constant(fnProg->returnType()));
            });
            break;
        }
        case ProgramKind::Global: {
            auto* globalProg = cast<GlobalProgram>(prog);
            output += "static ";
            switch (globalProg->globalKind()) {
            case GlobalKind::Let:
                output += "let ";
                break;
            case GlobalKind::Var:
                output += "var ";
                break;
            case GlobalKind::ConstVar:
                output += "const var ";
                break;
            case GlobalKind::OpenLet:
                output += "open let ";
                break;
            default:
                VERIFY_NOT_REACHED();
            }
            formatConstant(c);

            formatAs(progHandle, [&] {
                output += ": ";
                formatConstant(Constant(globalProg->type()));
                if (globalProg->globalKind() != GlobalKind::OpenLet && globalProg->hasInitializer()) {
                    output += " = ";
                    formatConstant(Constant(globalProg->initializer()));
                }
            });
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

}