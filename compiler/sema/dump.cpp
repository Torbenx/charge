#include <sema/Program.h>

#include <sstream>

namespace sema {

struct Dumper {
    struct IndentItem {
        bool atLast = false;
        uint8_t extraSpace = 0;
    };
    std::vector<IndentItem> indentation;
    std::string output;

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

    std::string formatValue(Value v) {
        if (v == INVALID_VALUE)
            return "<invalid>";
        char c;
        switch (v.kind()) {
        case ValueKind::Builtin: {
            switch ((BuiltinId)v.id()) {

#define BUILTIN(name, cppName) \
    case BuiltinId::cppName:   \
        return #name;
#include <sema/builtins.inc>

            default:
                VERIFY_NOT_REACHED();
            }
        }
        case ValueKind::Constant:
            c = 'c';
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
    for (int_t i = 0; i < (int_t)prog->constants.size(); i++) {
        const auto& c = prog->constants[i];
        std::string header = fmt::format("c{} = [{}]", i, formatValue(c.type));
        std::ostringstream line;
        line << header << nameString(c.op);
        switch (c.op) {
        case Program::Opcode::Parameterize: {
            const auto& param = c.u.parameterize;
            line << " " << formatValue(param.base) << "{";
            for (int_t i = 0; i < (int_t)param.argumentCount - 1; i++)
                line << formatValue(prog->parameterizeArguments[param.firstArgumentIndex + i]) << ", ";
            line << formatValue(prog->parameterizeArguments[param.firstArgumentIndex + param.argumentCount - 1]) << "}";
            break;
        }
        case Program::Opcode::SignatureOf:
        case Program::Opcode::ProgramLiteral: {
            line << " " << (void*)c.u.program;
            break;
        }
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
}

void Program::dump() {
    Dumper dumper;
    dumper.dumpProgram(this);
    std::cout << dumper.output;
}

}