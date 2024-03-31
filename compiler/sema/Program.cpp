#include <sema/Program.h>

namespace sema {

Value Program::add(Constant constant) {
    uint32_t id = constants.size();
    constants.push_back(constant);
    return Value(ValueKind::Constant, id);
}

Value Program::addExplicitParameter(Word name, Type type, std::optional<Value> defaultValue) {
    uint32_t parameterIndex = explicitParameters.size();
    Value result = add({
        Opcode::ExplicitParameter,
        type,
        { .parameter = { parameterIndex, defaultValue } },
    });
    explicitParameters.push_back({ name, result });
    return result;
}

Value Program::addImplicitParameter(Type type) {
    uint32_t parameterIndex = explicitParameters.size();
    Value result = add({
        Opcode::ImplicitParameter,
        type,
        { .parameter = { parameterIndex, {} } },
    });
    implicitParameters.push_back(result);
    return result;
}

Value Program::addExpression(Node* expr) {
    int_t size = expr->subTreeSize();
    expressions.insert(expressions.end(), expr - size + 1, expr + 1);
    return add({
        Opcode::Expression,
        Expression(expr).type(),
        { .expressionIndex = (uint32_t)(expressions.size() - 1) },
    });
}

Value Program::addLiteral(Type type, glue::DeclarationNode* node) {
    return add({
        Opcode::DeclarationNodeLiteral,
        type,
        { .declarationNode = node },
    });
}

Value Program::addProgramLiteral(Opcode op, Type type, Program* program) {
    VERIFY(op == Opcode::ProgramLiteral || op == Opcode::SignatureOf || op == Opcode::TypeOf);
    return add({
        op,
        type,
        { .program = program },
    });
}

Value Program::addStaticAccess(Type type, Value base, ExternValue value) {
    VERIFY(value.kind() == ValueKind::Constant);
    return add({
        Opcode::StaticAccess,
        type,
        { .access = { base, value.id() } },
    });
}

Program* Program::GetProgramLiteral(ExternValue value) {
    VERIFY(value.kind() == ValueKind::Constant);
    VERIFY(constants[value.id()].op == Opcode::ProgramLiteral);
    return constants[value.id()].u.program;
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