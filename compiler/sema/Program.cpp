#include <sema/Program.h>

namespace sema {

Value Program::add(Constant constant) {
    uint32_t id = constants.size();
    constants.push_back(constant);
    return Value(ValueKind::Constant, id);
}

Value Program::addParameter(Word name, Type type, std::optional<Value> defaultValue) {
    uint32_t parameterIndex = parameters.size();
    Value result = add({
        .op = Opcode::Parameter,
        .type = type,
        .u = { .parameterIndex = parameterIndex },
    });
    parameters.push_back({ name, result, defaultValue });
    return result;
}

Value Program::addExplicitParameter(Word name, Type type, std::optional<Value> defaultValue) {
    return addParameter(name, type, defaultValue);
}

Value Program::addImplicitParameter(Type type) {
    VERIFY(parameters.size() == inheritedParameterCount + implicitParameterCount);
    implicitParameterCount += 1;
    return addParameter(Word(), type, std::nullopt);
}

Value Program::addInheritedParameter(Type type, std::optional<Value> defaultValue) {
    VERIFY(parameters.size() == inheritedParameterCount);
    inheritedParameterCount += 1;
    return addParameter(Word(), type, defaultValue);
}

Value Program::addParameterize(Type type, Value base, int_t firstArgumentIndex, int_t argumentCount) {
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

std::pair<Value, Program::ParameterizeArgumentSetter> Program::addParameterize(Type type, Value base, int_t argumentCount) {
    auto firstIndex = parameterizeArguments.size();
    parameterizeArguments.resize(parameterizeArguments.size() + argumentCount, INVALID_VALUE);
    return { addParameterize(type, base, firstIndex, argumentCount), { this, (int_t)firstIndex } };
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

Value Program::addProgramLiteral(Opcode op, Type type, Program* program) {
    VERIFY(op == Opcode::ProgramLiteral || op == Opcode::SignatureOf);
    return add({
        .op = op,
        .type = type,
        .u = { .program = program },
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