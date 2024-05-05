#include <sema/Program.h>

namespace sema {

Value Program::add(Constant constant) {
    uint32_t id = constants.size();
    constants.push_back(constant);
    return Value(ValueKind::Constant, id);
}

Value Program::addParameterize(ProgramHandle base, int_t firstArgumentIndex, int_t argumentCount) {
    return add({
        .op = Opcode::Parameterize,
        .u = { .parameterize = {
                   .base = base,
                   .firstArgumentIndex = (uint16_t)firstArgumentIndex,
                   .argumentCount = (uint16_t)argumentCount,
               } },
    });
}

Value Program::addParameterize(ProgramHandle base, std::span<const Value> arguments) {
    VERIFY(!arguments.empty());
    auto firstIndex = parameterizeArguments.size();
    parameterizeArguments.insert(parameterizeArguments.end(), arguments.begin(), arguments.end());
    return addParameterize(base, firstIndex, arguments.size());
}

int_t Program::importNode(Node* node) {
    int_t size = node->subTreeSize();
    expressions.insert(expressions.end(), node - size + 1, node + 1);
    return expressions.size() - 1;
}

Value Program::addExpression(Node* expr) {
    return add({
        .op = Opcode::Expression,
        .u = { .expressionIndex = (uint32_t)importNode(expr) },
    });
}

Value Program::addNamespaceLiteral(glue::DeclarationNode* node) {
    return add({
        .op = Opcode::NamespaceLiteral,
        .u = { .declarationNode = node },
    });
}

Value Program::addTemplateSignature(ProgramHandle program) {
    return add({
        .op = Opcode::TemplateSignature,
        .u = { .templateSignature = program },
    });
}

Value Program::addFunctionSignature(Value value) {
    return add({
        .op = Opcode::FunctionSignature,
        .u = { .functionSignature = value },
    });
}

Value Program::addRemoteExpression(Value base, uint32_t expressionIndex) {
    return add({
        .op = Opcode::RemoteExpression,
        .u = { .remoteExpression = { base, expressionIndex } },
    });
}

std::string_view nameString(Program::Opcode op) {
    switch (op) {
#define PROGRAM_OP(kind)        \
    case Program::Opcode::kind: \
        return #kind;

        ENUMERATE_PROGRAM_OPS

#undef PROGRAM_OP
    default:
        VERIFY_NOT_REACHED();
    }
}

}