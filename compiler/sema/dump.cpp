#include <sema/Context.h>

#include <sstream>

namespace sema {

struct Dumper {
    Context& context;
    std::vector<std::string> indentation;
    std::string output;
    Program* program = nullptr;

    Dumper(Context& context)
        : context(context) { }

    void dumpProgram(Program*);
    void dumpInstruction(Instruction* inst);
    void beginLine() {
        for (const auto& entry : indentation)
            output += entry;
    }
    void dumpLine(std::string_view line) {
        beginLine();
        output += line;
        endLine();
    }
    void endLine() {
        output += '\n';
    }

    std::string formatScopeConstant(ScopeConstant value) {
        if (value.kind() == ConstantKind::Program)
            return formatProgram(value.program());
        if (value.kind() == ConstantKind::Namespace)
            return formatNamespace(value.nsHandle());
        return "<invalid scope value>";
    }
    std::string formatProgram(ProgramHandle progHandle) {
        Program* prog = context.program(progHandle);
        std::string name(context.wordTable.view(prog->name()));
        auto parent = prog->translate(prog->parent());
        if (parent.kind() == ConstantKind::Program)
            return formatProgram(parent.program()) + "::" + name;

        VERIFY(parent.kind() == ConstantKind::Namespace);
        auto path = formatNamespaceInternal(parent.nsHandle());
        if (path.empty())
            return name;
        return path + "::" + name;
    }
    std::string formatNamespaceInternal(NamespaceHandle nsHandle) {
        Namespace* ns = context.getNamespace(Constant(nsHandle).nsHandle());
        if (ns->name.empty())
            return {};
        std::string name(context.wordTable.view(ns->name));
        ns = context.getNamespace(ns->parent.value());
        while (!ns->name.empty()) {
            name = std::string(context.wordTable.view(ns->name)) + "::" + name;
            ns = context.getNamespace(ns->parent.value());
        }
        return name;
    }
    std::string formatNamespace(NamespaceHandle nsHandle) {
        std::string result = formatNamespaceInternal(nsHandle);
        if (result.empty())
            return "<global namespace>";
        return result;
    }
    std::string formatConstant(Constant v) {
        return formatConstant(program, v);
    }
    std::string formatConstant(Program* prog, Constant v) {
        if (v == INVALID_CONSTANT)
            return "<invalid>";
        std::string result;
        switch (v.kind()) {
        case ConstantKind::Program:
            return formatProgram(prog->translate(v.program()));
        case ConstantKind::Namespace:
            return formatNamespace(prog->translate(v.nsHandle()));
        case ConstantKind::TemplateSignature$Program:
        case ConstantKind::TemplateSignature$Parameterize:
            return "templsig(" + formatConstant(prog, v.templateSignatureBaseConstant()) + ")";
        case ConstantKind::FunctionSignature$Program:
        case ConstantKind::FunctionSignature$Parameterize:
            return "fnsig(" + formatConstant(prog, v.functionSignatureBaseConstant()) + ")";
        case ConstantKind::BooleanLiteral:
            return v.booleanValue() ? "true" : "false";
        case ConstantKind::Computed:
            result += "e";
            break;
        case ConstantKind::Parameterize:
            result += "p";
            break;
        case ConstantKind::RemoteComputed:
            result += "re";
            break;
        case ConstantKind::MemberPointer:
            result += "m";
            break;
        case ConstantKind::CopyOfParameter:
            result += '#';
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        result += std::to_string(v.id());
        return result;
    }

    std::string formatReference(Reference e) {
        if (e == INVALID_REFERENCE)
            return "<invalid>";
        std::string result;
        switch (e.kind()) {
        case ReferenceKind::Parameter:
            result += "arg";
            break;
        case ReferenceKind::TemplateParameter:
            result += "#";
            break;
        case ReferenceKind::LocalVariable:
            result += "var";
            break;
        case ReferenceKind::LocalReference:
            result += "ref";
            break;
        case ReferenceKind::MemberExpression: {
            auto memberExpr = program->getMemberReference(e);
            return formatReference(memberExpr.base)
                + "." + formatMember(program, program->getMemberPointer(memberExpr.memberPointer));
        }
        default:
            VERIFY_NOT_REACHED();
        }
        result += std::to_string(e.id());
        return result;
    }

    std::string formatExpressionResult(ExpressionResult expr) {
        if (expr.isConstant()) {
            return formatConstant(expr.constant());
        } else if (expr.isReference()) {
            return formatReference(expr.reference());
        } else {
            return "slot" + std::to_string(expr.valueSlot().index());
        }
    }

    std::string formatMember(Program* prog, MemberPointer pointer) {
        auto* parentProg = cast<TypeProgram>(context.program(prog->baseProgram(pointer.parentType)));
        const auto& member = parentProg->runtimeParameters[pointer.memberIndex];
        if (member.name.empty()) {
            VERIFY(member.kind() == RuntimeParameterKind::HasMember);
            return "(has " + formatConstant(parentProg, member.type()) + ")";
        } else {
            return (std::string)context.wordTable.view(member.name);
        }
    }
};

void Dumper::dumpInstruction(Instruction* inst) {
    std::stringstream line;
    switch (inst->opcode()) {
    case Opcode::Call: {
        auto* call = cast<CallInstruction>(inst);
        line << "[" << formatConstant(call->returnType) << "] " << formatConstant(call->callTarget) << " (";
        for (auto arg : call->arugments()) {
            line << formatExpressionResult(arg) << ", ";
        }
        line << ")";
        break;
    }
    case Opcode::ImplicitCopy: {
        line << "Copy " << formatReference(cast<ImplicitCopyInstruction>(inst)->copyFrom);
        break;
    }
    case Opcode::Branch: {
        auto* branchInst = cast<BranchInstruction>(inst);
        for (auto& branch : branchInst->branches()) {
            dumpLine("if " + formatExpressionResult(branch.conidition));
            indentation.push_back("  ");
            for (auto* inst : branch.body())
                dumpInstruction(inst);
            indentation.pop_back();
        }
        return;
    }
    case Opcode::Deactivate:
        line << "Deactivate " << formatReference(cast<DeactivateInstruction>(inst)->target);
        break;
    case Opcode::Discard:
        break;
    case Opcode::Initialize: {
        auto* init = cast<InitializeInstruction>(inst);
        line << formatReference(init->target) << " = " << formatExpressionResult(init->initializer);
        break;
    }
    default:
        VERIFY_NOT_REACHED();
    }
    dumpLine(line.str());
}

void Dumper::dumpProgram(Program* prog) {
    this->program = prog;
    dumpLine(formatProgram(context.programHandle(prog)) + ":");
    if (prog->status() >= ProgramStatus::SignatureCheckInProgress) {
        dumpLine("parent = " + formatScopeConstant(prog->translate(prog->parent())));
    }
    switch (prog->kind()) {
    case ProgramKind::Value:
        dumpLine("type = " + formatConstant((Constant)prog->m_type.value_or(INVALID_CONSTANT)));
        dumpLine("value = " + formatConstant(Constant::fromUint(prog->m_subClassData)));
        break;
    case ProgramKind::Object:
        dumpLine("object-type = " + formatConstant((Constant)prog->m_type.value_or(INVALID_CONSTANT)));
        break;
    case ProgramKind::Function:
        dumpLine("return-type = " + formatConstant((Constant)prog->m_type.value_or(INVALID_CONSTANT)));
        indentation.push_back("  ");
        for (auto* inst : cast<FunctionProgram>(prog)->body())
            dumpInstruction(inst);
        indentation.pop_back();
        break;
    default:
        break;
    }
    for (Constant value : std::views::join(std::array { program->parameterizeConstants(), program->memberPointerConstants(), program->computedConstants(), program->remoteComputedConstants() })) {
        std::ostringstream line;
        line << formatConstant(value) << " = ";
        switch (value.kind()) {
        case ConstantKind::Parameterize: {
            auto parameterize = program->getParameterize(value);
            line << formatProgram(parameterize.base) << "{";
            for (int_t i = 0; i < (int_t)parameterize.arguments.size() - 1; i++)
                line << formatConstant(parameterize.arguments[i]) << ", ";
            line << formatConstant(parameterize.arguments.back()) << "}";
            break;
        }
        case ConstantKind::Computed: {
            dumpLine(line.str());
            auto expr = program->getComputedConstant(value);
            indentation.push_back("  ");
            for (auto* inst : expr.body)
                dumpInstruction(inst);
            indentation.pop_back();
            continue;
        }
        case ConstantKind::RemoteComputed: {
            auto rExpr = program->getRemoteComputedConstant(value);
            VERIFY(rExpr.computation.kind() == ConstantKind::Computed);
            line << formatConstant(rExpr.base) << "/e" << rExpr.computation.id();
            break;
        }
        case ConstantKind::MemberPointer: {
            auto pointer = program->getMemberPointer(value);
            line << formatConstant(pointer.parentType) << "." << formatMember(prog, pointer);
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }
        dumpLine(line.str());
    }
    this->program = nullptr;
}

void Program::dump(Context& context) {
    Dumper dumper { context };
    dumper.dumpProgram(this);
    std::cout << dumper.output;
}

}