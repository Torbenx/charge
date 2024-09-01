#include <sema/Program.h>

namespace sema {

Value Program::addParameterize(ProgramHandle base, std::span<const Value> arguments) {
    VERIFY(!arguments.empty());
    auto id = valueData.size();
    valueData.push_back(Value(ValueKind::Parameterize, arguments.size()));
    valueData.push_back(Value(base));
    valueData.insert(valueData.end(), arguments.begin(), arguments.end());
    return Value(ValueKind::Parameterize, id);
}

Value Program::addExpression(Expression expr) {
    auto id = instructions.size();
    instructions.push_back({ Opcode::ExpressionHeader, {}, { .expressionSize = (uint32_t)expr.size } });
    instructions.insert(instructions.end(), expr.begin(), expr.end());
    return Value(ValueKind::Expression, id);
}

Value Program::addRemoteExpression(Value base, ExternValue expression) {
    VERIFY(expression.kind() == ValueKind::Expression);
    auto id = valueData.size();
    valueData.push_back(Value(ValueKind::RemoteExpression, expression.id()));
    valueData.push_back(base);
    return Value(ValueKind::RemoteExpression, id);
}

Value Program::addMemberPointer(Type parent, uint32_t memberIndex) {
    auto id = valueData.size();
    valueData.push_back(Value(ValueKind::MemberPointer, memberIndex));
    valueData.push_back(parent);
    return Value(ValueKind::MemberPointer, id);
}

}