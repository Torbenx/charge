#include <sema/Context.h>

#include <sstream>

namespace sema {

struct Dumper {
    Context& context;
    std::vector<std::string> indentation;
    std::string output;
    Program* program = nullptr;

    void popIndentation() {
        VERIFY(!indentation.empty());
        indentation.pop_back();
    }

    Dumper(Context& context)
        : context(context) { }

    void dumpProgram(Program*);
    void dumpInstructions(std::span<const Instruction> inst);
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

    std::string formatExpression(Expression e) {
        if (e == INVALID_EXPRESSION)
            return "<invalid>";
        if (e.isConstant())
            return formatConstant(e.constant());

        std::string result;
        switch (e.kind()) {
        case ExpressionKind::ParameterReference:
            result += "arg";
            break;
        case ExpressionKind::TemplateParameterReference:
            result += "#";
            break;
        case ExpressionKind::VariableReference:
            result += "var";
            break;
        case ExpressionKind::ReferenceReference:
            result += "ref";
            break;
        case ExpressionKind::MemberExpression: {
            auto memberExpr = program->getMemberReference(e);
            return formatExpression(memberExpr.base)
                + "." + formatMember(program, program->getMemberPointer(memberExpr.memberPointer));
        }
        case ExpressionKind::Call:
            result += "call";
            break;
        case ExpressionKind::ImplicitCopy:
            result += "copy";
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        result += std::to_string(e.id());
        return result;
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

void Dumper::dumpInstructions(std::span<const Instruction> instructions) {
    for (const auto& inst : instructions) {
        std::stringstream line;
        bool increaseIndentation = false;
        switch (inst.opcode()) {
        case Opcode::Call: {
            auto call = program->getCall(inst.u.callExpression);
            line << "[" << formatConstant(call.returnType) << "] " << formatConstant(call.callTarget) << "(";
            if (!call.arguments.empty()) {
                for (int_t i = 0; i < (int_t)call.arguments.size() - 1; i++)
                    line << formatExpression(call.arguments[i]) << ", ";
                line << formatExpression(call.arguments.back());
            }
            line << ")";
            break;
        }
        case Opcode::ImplicitCopy: {
            auto copy = program->getImplicitCopy(inst.u.implicitCopyExpression);
            line << "[" << formatConstant(copy.type) << "]"
                 << "Copy " << formatExpression(copy.copyFrom);
            break;
        }
        case Opcode::BlockScope:
            line << "block:";
            increaseIndentation = true;
            break;
        case Opcode::Branch:
            line << "if " << formatExpression(inst.u.scope.u.branchCondition) << ":";
            increaseIndentation = true;
            break;
        case Opcode::BranchContinued:
            line << "elif " << formatExpression(inst.u.scope.u.branchCondition) << ":";
            popIndentation();
            increaseIndentation = true;
            break;
        case Opcode::Deactivate:
            line << "Deactivate " << formatExpression(inst.u.deactivateTarget);
            break;
        case Opcode::Discard:
            line << "Discard " << formatExpression(inst.u.discardValue);
            break;
        case Opcode::Initialize:
            line << formatExpression(inst.u.initialize.target) << " = " << formatExpression(inst.u.initialize.value);
            break;
        case Opcode::EndScope:
            popIndentation();
            line << "end";
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        dumpLine(line.str());
        if (increaseIndentation)
            indentation.push_back("  ");
    }
}

void Dumper::dumpProgram(Program* prog) {
    this->program = prog;
    dumpLine(formatProgram(context.programHandle(prog)) + ":");
    if (prog->status() >= ProgramStatus::SignatureCheckInProgress) {
        dumpLine("parent = " + formatScopeConstant(prog->translate(prog->parent())));
    }
    switch (prog->kind()) {
    case ProgramKind::Global:
        dumpLine("type = " + formatConstant((Constant)prog->m_type.value_or(INVALID_CONSTANT)));
        dumpLine("value = " + formatConstant(Constant::fromUint(prog->m_subClassData)));
        break;
    case ProgramKind::Function:
        dumpLine("return-type = " + formatConstant((Constant)prog->m_type.value_or(INVALID_CONSTANT)));
        indentation.push_back("  ");
        dumpInstructions(cast<FunctionProgram>(prog)->body());
        popIndentation();
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
            auto expr = program->getComputedConstant(value);
            line << formatExpression(expr.value);
            dumpLine(line.str());
            indentation.push_back("  ");
            dumpInstructions(expr.body);
            popIndentation();
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