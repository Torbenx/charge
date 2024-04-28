#include <sema/Program.h>

namespace sema {

Value Program::add(Constant constant) {
    uint32_t id = constants.size();
    constants.push_back(constant);
    return Value(ValueKind::Constant, id);
}

Value Program::addParameterize(Type type, ProgramHandle base, int_t firstArgumentIndex, int_t argumentCount) {
    return add({
        .op = Opcode::Parameterize,
        .type = type,
        .u = { .parameterize = {
                   .base = base,
                   .firstArgumentIndex = (uint16_t)firstArgumentIndex,
                   .argumentCount = (uint16_t)argumentCount,
               } },
    });
}

Value Program::addParameterize(Type type, ProgramHandle base, std::span<const Value> arguments) {
    auto firstIndex = parameterizeArguments.size();
    parameterizeArguments.insert(parameterizeArguments.end(), arguments.begin(), arguments.end());
    return addParameterize(type, base, firstIndex, arguments.size());
}

Value Program::addExpression(Node* expr) {
    int_t size = expr->subTreeSize();
    expressions.insert(expressions.end(), expr - size + 1, expr + 1);
    return add({
        .op = Opcode::Expression,
        .type = Expression(expr).type(),
        .u = { .expressionIndex = (uint32_t)(expressions.size() - 1) },
    });
}

Value Program::addNamespaceLiteral(glue::DeclarationNode* node) {
    return add({
        .op = Opcode::NamespaceLiteral,
        .type = builtins::namespace_type,
        .u = { .declarationNode = node },
    });
}

Value Program::addTemplateSignatureOf(Type type, ProgramHandle program) {
    return add({
        .op = Opcode::TemplateSignatureOf,
        .type = type,
        .u = { .signatureProgram = program },
    });
}

Value Program::addFunctionSignatureOf(Type type, Value value) {
    return add({
        .op = Opcode::FunctionSignatureOf,
        .type = type,
        .u = { .signatureValue = value },
    });
}

Value Program::addRemoteExpression(Type type, Value base, uint32_t expressionIndex) {
    return add({
        .op = Opcode::RemoteExpression,
        .type = type,
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