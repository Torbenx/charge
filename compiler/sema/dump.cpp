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
    void dumpNode(Node*);
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
    std::string formatValue(Value v) {
        if (v == INVALID_VALUE)
            return "<invalid>";
        char c;
        switch (v.kind()) {
        case ValueKind::Program:
            return formatProgram(v.program());
        case ValueKind::Constant:
            c = 'c';
            break;
        case ValueKind::Parameter:
            c = '#';
            break;
        case ValueKind::Local:
            c = 'r';
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        return c + std::to_string(v.id());
    }
};

std::string Dumper::formatProgram(Program* base, ProgramHandle externHandle) {
    ProgramHandle translatedHandle = base->programTranslationBuffer[externHandle.id()];
    return formatProgram(&context.programs[translatedHandle.id()]);
}

std::string Dumper::formatProgram(Program* targetProg) {
    std::string name(context.wordTable.view(targetProg->name()));
    auto parentValue = targetProg->parent();
    if (parentValue.kind() == ValueKind::Program)
        return formatProgram(targetProg, parentValue.program()) + "::" + name;

    VERIFY(parentValue.kind() == ValueKind::Constant);
    const auto& parentConst = targetProg->constants[parentValue.id()];
    if (parentConst.op == Program::Opcode::Parameterize)
        return formatProgram(targetProg, parentConst.u.parameterize.base) + "::" + name;

    VERIFY(parentConst.op == Program::Opcode::NamespaceLiteral);
    glue::DeclarationNode* scope = parentConst.u.declarationNode;
    while (scope->declaringNode() != nullptr) {
        name = std::string(context.wordTable.view(scope->name())) + "::" + name;
        scope = scope->declaringNode();
    }
    return name;
}

void Dumper::dumpNode(Node* node) {
    std::vector<Node*> children(node->reverseChildren().begin(), node->reverseChildren().end());
    std::reverse(children.begin(), children.end());
    auto header = fmt::format("[{}]", formatValue(Expression(node).type()));
    if (children.empty()) {
        beginLine();
        output += header;
        output += nameString(node->kind());
        if (node->kind() == NodeKind::ReferenceExpr || node->kind() == NodeKind::ConstantExpr) {
            output += " ";
            output += formatValue(Value::fromUint(node->u.data2));
        }
        endLine();
        return;
    }

    dumpLine(fmt::format("{}{}", header, nameString(node->kind())));

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
    for (int_t i = 0; i < (int_t)prog->constants.size(); i++) {
        const auto& c = prog->constants[i];
        std::string header = fmt::format("c{} = [{}]", i, formatValue(c.type));
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
        case Program::Opcode::SignatureOf:
            line << " " << formatProgram(c.u.signatureProgram);
            break;
        default:
            break;
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
    }
    this->program = nullptr;
}

void Program::dump(glue::Context& context) {
    Dumper dumper { context };
    dumper.dumpProgram(this);
    std::cout << dumper.output;
}

}