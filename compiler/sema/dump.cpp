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

    std::string formatScopeValue(ScopeValue value) {
        if (value.kind() == ValueKind::Program)
            return formatProgram(value.program());
        if (value.kind() == ValueKind::Namespace)
            return formatNamespace(value.nsHandle());
        return "<invalid scope value>";
    }
    std::string formatProgram(ProgramHandle progHandle) {
        Program* prog = context.program(progHandle);
        std::string name(context.wordTable.view(prog->name()));
        auto parentValue = prog->translate(prog->parent());
        if (parentValue.kind() == ValueKind::Program)
            return formatProgram(parentValue.program()) + "::" + name;

        VERIFY(parentValue.kind() == ValueKind::Namespace);
        auto path = formatNamespaceInternal(parentValue.nsHandle());
        if (path.empty())
            return name;
        return path + "::" + name;
    }
    std::string formatNamespaceInternal(NamespaceHandle nsHandle) {
        Namespace* ns = context.getNamespace(Value(nsHandle).nsHandle());
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
    std::string formatValue(Value v) {
        return formatValue(program, v);
    }
    std::string formatValue(Program* prog, Value v) {
        if (v == INVALID_VALUE)
            return "<invalid>";
        std::string result;
        switch (v.kind()) {
        case ValueKind::Program:
            return formatProgram(prog->translate(v.program()));
        case ValueKind::Namespace:
            return formatNamespace(prog->translate(v.nsHandle()));
        case ValueKind::TemplateSignature$Program:
        case ValueKind::TemplateSignature$Parameterize:
            return "templsig(" + formatValue(prog, v.templateSignatureBaseValue()) + ")";
        case ValueKind::FunctionSignature$Program:
        case ValueKind::FunctionSignature$Parameterize:
            return "fnsig(" + formatValue(prog, v.functionSignatureBaseValue()) + ")";
        case ValueKind::BooleanLiteral:
            return v.booleanValue() ? "true" : "false";
        case ValueKind::Expression:
            result += "e";
            break;
        case ValueKind::Parameterize:
            result += "p";
            break;
        case ValueKind::RemoteExpression:
            result += "re";
            break;
        case ValueKind::MemberPointer:
            result += "m";
            break;
        case ValueKind::CopyOfParameter:
            result += '#';
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        result += std::to_string(v.id());
        return result;
    }

    std::string formatReferenceExpression(ReferenceExpression e) {
        if (e == INVALID_REFERENCE_EXPRESSION)
            return "<invalid>";
        std::string result;
        switch (e.kind()) {
        case ReferenceExpressionKind::Parameter:
            result += "arg";
            break;
        case ReferenceExpressionKind::TemplateParameter:
            result += "#";
            break;
        case ReferenceExpressionKind::LocalVariable:
            result += "var";
            break;
        case ReferenceExpressionKind::LocalReference:
            result += "ref";
            break;
        case ReferenceExpressionKind::MemberExpression: {
            auto memberExpr = program->getMemberReferenceExpression(e);
            return formatReferenceExpression(memberExpr.base)
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
            return "(has " + formatValue(parentProg, member.type()) + ")";
        } else {
            return (std::string)context.wordTable.view(member.name);
        }
    }
};

void Dumper::dumpInstruction(Instruction inst) {
    std::stringstream line, info;
    if (isExpression(inst.opcode()))
        line << "[" << formatValue(inst.u.expr.type) << "]";

    line << nameString(inst.opcode());
    switch (inst.opcode()) {
    case Opcode::VarDecl: {
        auto decl = inst.u.decl;
        info << "[" << formatValue(decl.type) << "]r" << decl.localValueIndex;
        break;
    }
    case Opcode::Reference:
        info << formatReferenceExpression(inst.u.expr.u.referenceExpr);
        break;
    case Opcode::Constant:
        info << formatValue(inst.u.expr.u.constant);
        break;
    case Opcode::Call:
        info << formatValue(inst.u.expr.u.callTarget);
        break;
    case Opcode::RMemberAccess:
        info << formatValue(inst.u.expr.u.memberPointer);
        break;
    case Opcode::Jump:
    case Opcode::JumpIf:
        info << fmt::format("{:+}", inst.u.jumpDistance);
        break;
    case Opcode::Deactivate:
        info << formatReferenceExpression(inst.u.deactivateTarget);
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
        dumpLine("parent = " + formatScopeValue(prog->translate(prog->parent())));
    }
    switch (prog->kind()) {
    case ProgramKind::Value:
        dumpLine("type = " + formatValue((Value)prog->m_type.value_or(INVALID_VALUE)));
        dumpLine("value = " + formatValue(Value::fromUint(prog->m_subClassData)));
        break;
    case ProgramKind::Object:
        dumpLine("object-type = " + formatValue((Value)prog->m_type.value_or(INVALID_VALUE)));
        break;
    case ProgramKind::Function:
        dumpLine("return-type = " + formatValue((Value)prog->m_type.value_or(INVALID_VALUE)));
        // dumpNode(cast<FunctionProgram>(prog)->body(), "body = ");
        break;
    default:
        break;
    }
    for (Value value : std::views::join(std::array { program->parameterizeValues(), program->memberPointerValues(), program->remoteExpressionValues() })) {
        std::ostringstream line;
        line << formatValue(value) << " = ";
        switch (value.kind()) {
        case ValueKind::Parameterize: {
            auto parameterize = program->getParameterize(value);
            line << formatProgram(parameterize.base) << "{";
            for (int_t i = 0; i < (int_t)parameterize.arguments.size() - 1; i++)
                line << formatValue(parameterize.arguments[i]) << ", ";
            line << formatValue(parameterize.arguments.back()) << "}";
            break;
        }
        case ValueKind::RemoteExpression: {
            auto rExpr = program->getRemoteExpression(value);
            VERIFY(rExpr.expression.kind() == ValueKind::Expression);
            line << formatValue(rExpr.base) << "/e" << rExpr.expression.id();
            break;
        }
        case ValueKind::MemberPointer: {
            auto pointer = program->getMemberPointer(value);
            line << formatValue(pointer.parentType) << "." << formatMember(prog, pointer);
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