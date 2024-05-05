#include <glue/Context.h>
#include <sema/Program.h>

#include <sstream>

namespace sema {

struct Dumper {
    struct IndentItem {
        bool atLast = false;
        uint8_t extraSpace = 0;
    };
    glue::Context& context;
    std::vector<IndentItem> indentation;
    std::string output;
    Program* program = nullptr;

    Dumper(glue::Context& context)
        : context(context) { }

    void dumpProgram(Program*);
    void dumpNode(Node*, std::string header = "");
    void beginLine() {
        if (!indentation.empty()) {
            for (int_t i = 0; i < (int_t)indentation.size() - 1; i++) {
                auto item = indentation[i];
                output += std::string(item.extraSpace, ' ');
                output += item.atLast ? "  " : "| ";
            }
            output += std::string(indentation.back().extraSpace, ' ');
            output += indentation.back().atLast ? "'-" : "|-";
        }
    }
    void dumpLine(std::string_view line) {
        beginLine();
        output += line;
        endLine();
    }
    void endLine() {
        output += '\n';
    }

    std::string formatProgram(ProgramHandle handle) { return formatProgram(program, handle); }
    std::string formatProgram(Program* base, ProgramHandle);
    std::string formatProgram(Program*);
    std::string formatNamespaceInternal(ExternValue nsHandle);
    std::string formatNamespace(ExternValue nsHandle) {
        std::string result = formatNamespaceInternal(nsHandle);
        if (result.empty())
            return "<global namespace>";
        return result;
    }
    std::string formatValue(Value v) {
        if (v == INVALID_VALUE)
            return "<invalid>";
        std::string result;
        switch (v.kind()) {
        case ValueKind::Program:
            return formatProgram(v.program());
        case ValueKind::Namespace:
            return formatNamespace(v);
        case ValueKind::TemplateSignature:
            return "templsig(" + formatValue(v.templateSignatureBaseValue()) + ")";
        case ValueKind::FunctionSignature$Program:
        case ValueKind::FunctionSignature$Parameterize:
            return "fnsig(" + formatValue(v.functionSignatureBaseValue()) + ")";
        case ValueKind::Expression:
            result += "e";
            break;
        case ValueKind::Parameterize:
        case ValueKind::RemoteExpression:
            result += "d";
            break;
        case ValueKind::Parameter:
            result += '#';
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        result += std::to_string(v.id());
        return result;
    }
};

std::string Dumper::formatProgram(Program* base, ProgramHandle externHandle) {
    ProgramHandle translatedHandle = base->programTranslationBuffer[externHandle.id()];
    return formatProgram(context.program(translatedHandle));
}

std::string Dumper::formatProgram(Program* targetProg) {
    std::string name(context.wordTable.view(targetProg->name()));
    auto parentValue = targetProg->parent();
    if (parentValue.kind() == ValueKind::Program)
        return formatProgram(targetProg, parentValue.program()) + "::" + name;

    if (parentValue.kind() == ValueKind::Parameterize)
        return formatProgram(targetProg, targetProg->getParameterize(parentValue).base) + "::" + name;

    VERIFY(parentValue.kind() == ValueKind::Namespace);
    auto path = formatNamespaceInternal(parentValue);
    if (path.empty())
        return name;
    return path + "::" + name;

}

std::string Dumper::formatNamespaceInternal(ExternValue nsHandle) {
    glue::DeclarationNode* scope = context.getNamespace((Value)nsHandle);
    std::string name(context.wordTable.view(scope->name()));
    while (scope->declaringNode() != nullptr) {
        scope = scope->declaringNode();
        name = std::string(context.wordTable.view(scope->name())) + "::" + name;
    }
    return name;
}

static std::vector<Node*> allChildren(ChildrenRange range) {
    std::vector<Node*> children(range.begin(), range.end());
    std::reverse(children.begin(), children.end());
    return children;
}

void Dumper::dumpNode(Node* node, std::string header) {
    if (isExpression(node->kind()))
        header += fmt::format("[{}]", formatValue(Expression(node).type()));
    std::string info;
    switch (node->kind()) {
    case NodeKind::ConstantExpr:
        info += " ";
        info += formatValue(Expression(node).data().constant);
        break;
    case NodeKind::CallExpr:
        info += " ";
        info += formatValue(Expression(node).data().callBase);
        break;
    default:
        break;
    }
    dumpLine(header + std::string(nameString(node->kind())) + info);

    auto children = allChildren(node);
    if (children.empty())
        return;
    indentation.emplace_back(false, header.size());
    for (int_t i = 0; i < (int_t)children.size() - 1; i++) {
        dumpNode(children[i]);
    }
    indentation.pop_back();
    indentation.emplace_back(true, header.size());
    dumpNode(children.back());
    indentation.pop_back();
}

void Dumper::dumpProgram(Program* prog) {
    this->program = prog;
    dumpLine(formatProgram(prog) + ":");
    dumpLine("parent = " + formatValue((Value)prog->m_parent.value_or(INVALID_VALUE)));
    dumpLine("self = " + formatValue((Value)prog->m_self.value_or(INVALID_VALUE)));
    dumpLine("type = " + formatValue((Value)prog->m_type.value_or(INVALID_VALUE)));
    switch (prog->kind()) {
    case ProgramKind::Value:
        dumpLine("value = " + formatValue(Value::fromUint(prog->m_subClassData)));
        break;
    case ProgramKind::Function:
        dumpNode(static_cast<FunctionProgram*>(prog)->body(), "body = ");
        break;
    default:
        break;
    }
    for (Value value : program->dataValues()) {
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
            line << formatValue(rExpr.base) << "/e" << rExpr.expressionIndex;
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }
        dumpLine(line.str());
    }
    /*for (int_t i = 0; i < (int_t)prog->valueData.size(); i++) {
        const auto& c = prog->constants[i];
        std::string header = fmt::format("c{} = ", i);
        std::ostringstream line;
        line << header << nameString(c.op);
        switch (c.op) {
        case Program::Opcode::Parameterize: {
            const auto& param = c.u.parameterize;
            line << " " << formatProgram(param.base) << "{";
            for (int_t i = 0; i < (int_t)param.argumentCount - 1; i++)
                line << formatValue(prog->parameterizeArguments[param.firstArgumentIndex + i]) << ", ";
            line << formatValue(prog->parameterizeArguments[param.firstArgumentIndex + param.argumentCount - 1]) << "}";
            break;
        }
        }
        dumpLine(line.view());
        switch (c.op) {
        case Program::Opcode::Expression:
            indentation.emplace_back(true, header.size());
            dumpNode(&prog->expressions[c.u.expressionIndex]);
            indentation.pop_back();
            break;
        default:
            break;
        }
    }*/
    auto nodes = allChildren(program->topLevelNodes());
    for (Node* node : nodes) {
        int_t index = node - program->expressions.data();
        dumpNode(node, fmt::format("e{} = ", index));
    }
    this->program = nullptr;
}

void Program::dump(glue::Context& context) {
    Dumper dumper { context };
    dumper.dumpProgram(this);
    std::cout << dumper.output;
}

}