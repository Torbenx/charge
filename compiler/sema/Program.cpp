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

int_t Program::importNode(Node* node) {
    int_t size = node->subTreeSize();
    expressions.insert(expressions.end(), node - size + 1, node + 1);
    return expressions.size() - 1;
}

Value Program::addExpression(Node* expr) {
    return Value(ValueKind::Expression, importNode(expr));
}

Value Program::addRemoteExpression(Value base, uint32_t expressionIndex) {
    auto id = valueData.size();
    valueData.push_back(Value(ValueKind::RemoteExpression, expressionIndex));
    valueData.push_back(base);
    return Value(ValueKind::RemoteExpression, id);
}

}