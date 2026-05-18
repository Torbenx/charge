#include <sema/Context.h>

#include <sstream>

namespace sema {

struct Dumper {
    Context& context;
    std::vector<std::string> indentation;
    std::string output;
    Program* program = nullptr;
    ProgramHandle programHandle;

    void popIndentation() {
        VERIFY(!indentation.empty());
        indentation.pop_back();
    }

    Dumper(Context& context)
        : context(context) { }

    void dumpProgram(ProgramHandle);
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

    std::string formatDeclarationValue(DeclarationValue value) {
        switch (value.kind()) {
        case DeclarationValueKind::Program:
            return formatProgram(value.program());
        case DeclarationValueKind::Namespace:
            return formatNamespace(value.nsHandle());
        default:
            return "<invalid scope value>";
        }
    }
    std::string formatProgram(ProgramHandle progHandle) {
        Program* prog = context.program(progHandle);
        std::string name;
        if (prog->isImpl())
            name = "(impl " + formatConstant(progHandle, (Constant)prog->selfConstant()) + ")";
        else
            name = context.tokenBuffer.wordTable.view(prog->name());
        auto parent = context.translate(progHandle, prog->parent());
        if (parent.kind() == DeclarationValueKind::Program)
            return formatProgram(parent.program()) + "::" + name;

        VERIFY(parent.kind() == DeclarationValueKind::Namespace);
        auto path = formatNamespaceInternal(parent.nsHandle());
        if (path.empty())
            return name;
        return path + "::" + name;
    }
    std::string formatNamespaceInternal(NamespaceHandle nsHandle) {
        Namespace* ns = context.getNamespace(Constant(nsHandle).nsHandle());
        if (ns->name.empty())
            return {};
        std::string name(context.tokenBuffer.wordTable.view(ns->name));
        ns = context.getNamespace(ns->parent.value());
        while (!ns->name.empty()) {
            name = std::string(context.tokenBuffer.wordTable.view(ns->name)) + "::" + name;
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
        return formatConstant(programHandle, v);
    }
    std::string formatConstant(ProgramHandle progHandle, Constant v) {
        if (v == INVALID_CONSTANT)
            return "<invalid>";
        std::string result;
        switch (v.kind()) {
        case ConstantKind::Program:
            return formatProgram(context.translate(progHandle, v.program()));
        case ConstantKind::Namespace:
            return formatNamespace(context.translate(progHandle, v.nsHandle()));
        case ConstantKind::TemplateSignature$Program:
        case ConstantKind::TemplateSignature$Parameterize:
            return "templsig(" + formatConstant(progHandle, v.templateSignatureBaseConstant()) + ")";
        case ConstantKind::FunctionSignature$Program:
        case ConstantKind::FunctionSignature$Parameterize:
            return "fnsig(" + formatConstant(progHandle, v.functionSignatureBaseConstant()) + ")";
        case ConstantKind::BooleanLiteral:
            return v.booleanValue() ? "true" : "false";
        case ConstantKind::ExpressionCategoryLiteral:
            switch (v.expressionCategory()) {
            case ExpressionCategory::Value:
                return "expression_category::value";
            case ExpressionCategory::UniqueReference:
                return "expression_category::unique_ref";
            case ExpressionCategory::SharedReference:
                return "expression_category::shared_ref";
            case ExpressionCategory::ConstUniqueReference:
                return "expression_category::const_unique_ref";
            case ExpressionCategory::ConstSharedReference:
                return "expression_category::const_shared_ref";
            default:
                VERIFY_NOT_REACHED();
            }
        case ConstantKind::EnumValue:
            result += "ev";
            break;
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
        case ConstantKind::CopyOfOpenGlobal$Program:
        case ConstantKind::CopyOfOpenGlobal$Parameterize:
            return formatConstant(v.copiedGlobal());
        case ConstantKind::OpenReturnType$Self:
        case ConstantKind::OpenReturnType$Parameterize:
            return formatConstant(v.returnTypeOf()) + "::return_type";
        case ConstantKind::CopyOfParameterToReferenceCategory:
            return "toReferenceCategory(" + formatConstant(v.originalExpressionCategory()) + ")";
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
        case ExpressionKind::GlobalReference$Program:
        case ExpressionKind::GlobalReference$Parameterize:
            return formatConstant(e.referencedGlobal());
        case ExpressionKind::VariableReference:
            result += "var";
            break;
        case ExpressionKind::ReferenceReference:
            result += "ref";
            break;
        case ExpressionKind::MemberExpression: {
            auto memberExpr = program->getMemberExpression(e);
            return formatExpression(memberExpr.base)
                + "." + formatMember(programHandle, program->getMemberPointer(memberExpr.memberPointer));
        }
        case ExpressionKind::Call:
            result += "call";
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        result += std::to_string(e.id());
        return result;
    }

    std::string formatMember(ProgramHandle progHandle, MemberPointer pointer) {
        std::string result;
        for (auto elem : pointer.elements) {
            if (!result.empty())
                result += ".";
            ProgramHandle parentProgHandle = context.program(progHandle)->baseProgram(elem.structImpl).value();
            auto* parentProg = cast<StructProgram>(context.program(parentProgHandle));
            const auto& member = parentProg->members[elem.memberIndex];
            if (member.isBase())
                result += "(base " + formatConstant(parentProgHandle, (Constant)member.type()) + ")";
            else
                result += context.tokenBuffer.wordTable.view(member.name());
        }
        return result;
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

void Dumper::dumpProgram(ProgramHandle progHandle) {
    this->programHandle = progHandle;
    this->program = context.program(programHandle);
    dumpLine(formatProgram(programHandle) + ":");
    if (program->status() >= ProgramStatus::SignatureCheckInProgress) {
        dumpLine("parent = " + formatDeclarationValue(context.translate(progHandle, program->parent())));
    }
    switch (program->kind()) {
    case ProgramKind::Global:
        dumpLine("type = " + formatConstant((Constant)program->m_type.value_or(INVALID_CONSTANT)));
        dumpLine("value = " + formatConstant(Constant::fromUint(program->m_subClassData)));
        break;
    case ProgramKind::Function:
        dumpLine("return-type = " + formatConstant((Constant)program->m_type.value_or(INVALID_CONSTANT)));
        indentation.push_back("  ");
        dumpInstructions(cast<FunctionProgram>(program)->body());
        popIndentation();
        break;
    default:
        break;
    }
    for (Constant value : std::views::join(std::array { program->parameterizeConstants(), program->memberPointerConstants(), program->computedConstants(), program->remoteComputedConstants(), program->enumValueConstants() })) {
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
            if (pointer.isIdentity()) {
                line << formatConstant(pointer.memberType) << ".self";
            } else {
                line << formatConstant(pointer.elements.front().structImpl) << "." << formatMember(programHandle, pointer);
            }
            break;
        }
        case ConstantKind::EnumValue: {
            auto enumValue = program->getEnumValue(value);
            auto* enumProg = cast<EnumProgram>(context.program(program->baseProgram(enumValue.enumType).value()));
            line << formatConstant((Constant)enumValue.enumType) << "::" << context.tokenBuffer.wordTable.view(enumProg->values[enumValue.valueIndex].name());
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
    dumper.dumpProgram(context.ownProgramHandle(this));
    print("{}", dumper.output);
}

}