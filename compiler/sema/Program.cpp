#include <sema/Program.h>

#include <sema/Util.h>
#include <sema/Context.h>

namespace sema {

uint32_t ParameterizeSet::get(Context& context, ProgramHandle prog, Parameterize para) {
    return Base::get(context, prog, para);
}
uint32_t ParameterizeSet::makeNode(Context&, ProgramHandle, Parameterize para, TreeLabel label) {
    return Base::makeNode(label, { para.base, { para.arguments.begin(), para.arguments.end() } });
}
std::strong_ordering ParameterizeSet::compare(Context& context, ProgramHandle program, Parameterize a, ParameterizeData& b) {
    return Util(context, program).compare(a, Parameterize::fromData(b));
}

uint32_t RemoteExpressionSet::get(Context& context, ProgramHandle prog, RemoteExpression expr) {
    return Base::get(context, prog, expr);
}
uint32_t RemoteExpressionSet::makeNode(Context&, ProgramHandle, RemoteExpression expr, TreeLabel label) {
    return Base::makeNode(label, expr);
}
std::strong_ordering RemoteExpressionSet::compare(Context& context, ProgramHandle program, RemoteExpression a, RemoteExpression b) {
    return Util(context, program).compare(a, b);
}

uint32_t MemberPointerSet::get(Context& context, ProgramHandle prog, MemberPointer ptr) {
    return Base::get(context, prog, ptr);
}
uint32_t MemberPointerSet::makeNode(Context&, ProgramHandle, MemberPointer ptr, TreeLabel label) {
    return Base::makeNode(label, ptr);
}
std::strong_ordering MemberPointerSet::compare(Context& context, ProgramHandle program, MemberPointer a, MemberPointer b) {
    return Util(context, program).compare(a, b);
}

Value Program::addParameterize(Context& context, Parameterize para) {
    VERIFY(!para.arguments.empty());
    auto id = parameterizes.get(context, context.programHandle(this), para);
    return Value(ValueKind::Parameterize, id);
}

Value Program::addRemoteExpression(Context& context, RemoteExpression expr) {
    VERIFY(expr.expression.kind() == ValueKind::Expression);
    auto id = remoteExpressions.get(context, context.programHandle(this), expr);
    return Value(ValueKind::RemoteExpression, id);
}

Value Program::addMemberPointer(Context& context, MemberPointer ptr) {
    auto id = memberPointers.get(context, context.programHandle(this), ptr);
    return Value(ValueKind::MemberPointer, id);
}

ReferenceExpression Program::addMemberReferenceExpression(MemberReferenceExpression e) {
    auto id = memberReferenceExpressions.size();
    memberReferenceExpressions.push_back(e);
    return ReferenceExpression(ReferenceExpressionKind::MemberExpression, id);
}

int_t Program::importInstructions(Opcode headerCode, std::span<const Instruction> stream) {
    int_t offset = instructions.size();
    instructions.push_back({ headerCode, {}, { .blockSize = (uint32_t)stream.size() } });
    instructions.insert(instructions.end(), stream.begin(), stream.end());
    return offset;
}

Value Program::addExpression(Expression expr) {
    return Value(ValueKind::Expression, importInstructions(Opcode::ExpressionHeader, expr.span()));
}

}