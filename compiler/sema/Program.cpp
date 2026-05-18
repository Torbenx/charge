#include <sema/Program.h>

#include <sema/Context.h>
#include <sema/Util.h>

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

uint32_t RemoteComputationSet::get(Context& context, ProgramHandle prog, RemoteComputation expr) {
    return Base::get(context, prog, expr);
}
uint32_t RemoteComputationSet::makeNode(Context&, ProgramHandle, RemoteComputation expr, TreeLabel label) {
    return Base::makeNode(label, expr);
}
std::strong_ordering RemoteComputationSet::compare(Context& context, ProgramHandle program, RemoteComputation a, RemoteComputation b) {
    return Util(context, program).compare(a, b);
}

uint32_t MemberPointerSet::get(Context& context, ProgramHandle prog, MemberPointer ptr) {
    return Base::get(context, prog, ptr);
}
uint32_t MemberPointerSet::makeNode(Context&, ProgramHandle, MemberPointer ptr, TreeLabel label) {
    return Base::makeNode(label, MemberPointerData { ptr.memberType, { ptr.elements.begin(), ptr.elements.end() } });
}
std::strong_ordering MemberPointerSet::compare(Context& context, ProgramHandle program, MemberPointer a, MemberPointer b) {
    return Util(context, program).compare(a, b);
}

uint32_t EnumValueSet::get(Context& context, ProgramHandle prog, EnumValue value) {
    return Base::get(context, prog, value);
}
uint32_t EnumValueSet::makeNode(Context&, ProgramHandle, EnumValue value, TreeLabel label) {
    return Base::makeNode(label, value);
}
std::strong_ordering EnumValueSet::compare(Context& context, ProgramHandle program, EnumValue a, EnumValue b) {
    return Util(context, program).compare(a, b);
}

Constant Program::addParameterize(Context& context, Parameterize para) {
    VERIFY(!para.arguments.empty());
    auto id = parameterizes.get(context, context.ownProgramHandle(this), para);
    return Constant(ConstantKind::Parameterize, id);
}

Constant Program::addRemoteComputedConstant(Context& context, RemoteComputation expr) {
    VERIFY(expr.computation.kind() == ConstantKind::Computed);
    auto id = remoteComputations.get(context, context.ownProgramHandle(this), expr);
    return Constant(ConstantKind::RemoteComputed, id);
}

Constant Program::addMemberPointer(Context& context, MemberPointer ptr) {
    auto id = memberPointers.get(context, context.ownProgramHandle(this), ptr);
    return Constant(ConstantKind::MemberPointer, id);
}

Constant Program::addEnumValue(Context& context, EnumValue value) {
#define BUILTIN_ENUM(name, constant_kind)        \
    if (value.enumType == builtins::name##_type) \
        return Constant(ConstantKind::constant_kind, value.valueIndex);
#include <sema/builtins.inc>

    auto id = enumValues.get(context, context.ownProgramHandle(this), value);
    return Constant(ConstantKind::EnumValue, id);
}

Constant Program::addComputedConstant(Context&, ComputedConstant c) {
    auto id = computations.size();
    computations.push_back({ c.value, c.type, { c.body.begin(), c.body.end() } });
    return Constant(ConstantKind::Computed, id);
}

Expression Program::addMemberExpression(MemberExpression e) {
    auto id = memberExpressions.size();
    memberExpressions.push_back(e);
    return Expression(ExpressionKind::MemberExpression, id);
}

Expression Program::addCall(CallData c) {
    auto id = calls.size();
    calls.emplace_back(std::move(c));
    return Expression(ExpressionKind::Call, id);
}

}