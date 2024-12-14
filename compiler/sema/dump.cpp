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
    void dumpInstruction(Instruction inst);
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
        case ConstantKind::Expression:
            result += "e";
            break;
        case ConstantKind::Parameterize:
            result += "p";
            break;
        case ConstantKind::RemoteExpression:
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

void Dumper::dumpInstruction(Instruction inst) {
    std::stringstream line, info;
    if (isExpression(inst.opcode()))
        line << "[" << formatConstant(inst.u.expr.type) << "]";

    line << nameString(inst.opcode());
    switch (inst.opcode()) {
    case Opcode::VarDecl: {
        auto decl = inst.u.decl;
        info << "[" << formatConstant(decl.type) << "]r" << decl.localValueIndex;
        break;
    }
    case Opcode::Reference:
        info << formatReference(inst.u.expr.u.reference);
        break;
    case Opcode::Constant:
        info << formatConstant(inst.u.expr.u.constant);
        break;
    case Opcode::Call:
        info << formatConstant(inst.u.expr.u.callTarget);
        break;
    case Opcode::RMemberAccess:
        info << formatConstant(inst.u.expr.u.memberPointer);
        break;
    case Opcode::Jump:
    case Opcode::JumpIf:
        info << fmt::format("{:+}", inst.u.jumpDistance);
        break;
    case Opcode::Deactivate:
        info << formatReference(inst.u.deactivateTarget);
        break;
    default:
        break;
    }
    if (auto instStr = info.str(); !instStr.empty())
        line << " " << instStr;
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
        // dumpNode(cast<FunctionProgram>(prog)->body(), "body = ");
        break;
    default:
        break;
    }
    for (Constant value : std::views::join(std::array { program->parameterizeConstants(), program->memberPointerConstants(), program->remoteExpressionConstants() })) {
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
        case ConstantKind::RemoteExpression: {
            auto rExpr = program->getRemoteExpression(value);
            VERIFY(rExpr.expression.kind() == ConstantKind::Expression);
            line << formatConstant(rExpr.base) << "/e" << rExpr.expression.id();
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
    for (auto block : program->instructionBlocks()) {
        beginLine();
        if (block.headerCode() == Opcode::ExpressionHeader) {
            output += "e" + std::to_string(program->instructions.data() - block.header()) + ":";
        } else {
            output += nameString(block.headerCode());
        }
        endLine();

        indentation.emplace_back("  ");
        for (const auto& inst : block)
            dumpInstruction(inst);
        indentation.pop_back();
    }
    this->program = nullptr;
}

void Program::dump(Context& context) {
    Dumper dumper { context };
    dumper.dumpProgram(this);
    std::cout << dumper.output;
}

}