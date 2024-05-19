#include <sema/Context.h>

#include <sstream>

namespace sema {

struct Dumper {
    struct IndentItem {
        bool atLast = false;
        uint8_t extraSpace = 0;
    };
    Context& context;
    std::vector<IndentItem> indentation;
    std::string output;
    Program* program = nullptr;

    Dumper(Context& context)
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
        if (v == INVALID_VALUE)
            return "<invalid>";
        std::string result;
        switch (v.kind()) {
        case ValueKind::Program:
            return formatProgram(program->translate(v.program()));
        case ValueKind::Namespace:
            return formatNamespace(program->translate(v.nsHandle()));
        case ValueKind::TemplateSignature$Program:
        case ValueKind::TemplateSignature$Parameterize:
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

static std::vector<Node*> allChildren(ChildrenRange range) {
    auto children = asVector(range);
    std::reverse(children.begin(), children.end());
    return children;
}

void Dumper::dumpNode(Node* node, std::string header) {
    if (isExpression(node->kind()))
        header += fmt::format("[{}]", formatValue(Expression(node).type()));
    std::stringstream info;
    switch (node->kind()) {
    case NodeKind::LetDecl: {
        auto decl = NodeHandle(node).data().decl;
        info << "[" << formatValue(decl.type) << "]r" << decl.localValueIndex;
        break;
    }
    case NodeKind::ReferenceExpr:
        info << "r" << Expression(node).data().localValueIndex;
        break;
    case NodeKind::ConstantExpr:
        info << formatValue(Expression(node).data().constant);
        break;
    case NodeKind::CallExpr:
        info << formatValue(Expression(node).data().callTarget);
        break;
    default:
        break;
    }
    auto infoStr = info.str();
    if (!infoStr.empty())
        infoStr.insert(infoStr.begin(), ' ');
    dumpLine(header + std::string(nameString(node->kind())) + infoStr);

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
    dumpLine(formatProgram(context.programHandle(prog)) + ":");
    if (prog->status() >= ProgramStatus::SignatureCheckInProgress) {
        dumpLine("parent = " + formatScopeValue(prog->translate(prog->parent())));
    }
    switch (prog->kind()) {
    case ProgramKind::Value:
        dumpLine("type = " + formatValue((Value)prog->m_type.value_or(INVALID_VALUE)));
        dumpLine("value = " + formatValue(Value::fromUint(prog->m_subClassData)));
        break;
    case ProgramKind::Function:
        dumpLine("return-type = " + formatValue((Value)prog->m_type.value_or(INVALID_VALUE)));
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
    auto nodes = allChildren(program->topLevelNodes());
    for (Node* node : nodes) {
        if (isExpression(node->kind())) {
            int_t index = node - program->expressions.data();
            dumpNode(node, fmt::format("e{} = ", index));
        }
    }
    this->program = nullptr;
}

void Program::dump(Context& context) {
    Dumper dumper { context };
    dumper.dumpProgram(this);
    std::cout << dumper.output;
}

}