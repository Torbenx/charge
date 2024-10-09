#include <sema/Util.h>

#include <sema/Context.h>

namespace sema {

Util::Util(Context& context, ProgramHandle programHandle)
    : context(context), program(context.program(programHandle)), programHandle(programHandle) { }

Program* Util::get(ProgramHandle progHandle) {
    return context.program(program->translate(progHandle));
}

Namespace* Util::get(NamespaceHandle progHandle) {
    return context.getNamespace(program->translate(progHandle));
}

std::strong_ordering Util::compare(Value a, Value b) {
    auto kindOrdering = a.kind() <=> b.kind();
    if (kindOrdering != 0)
        return kindOrdering;

    switch (a.kind()) {
        case ValueKind::Program:
            return compare(a.program(), b.program());
        case ValueKind::Namespace:
            return compare(a.nsHandle(), b.nsHandle());
        case ValueKind::TemplateSignature$Program:
            return compare(a.templateSignatureProgram(), b.templateSignatureProgram());
        case ValueKind::TemplateSignature$Parameterize:
            return compare(a.templateSignatureBaseValue(), b.templateSignatureBaseValue());
        case ValueKind::FunctionSignature$Program:
            return compare(a.functionSignatureProgram(), b.functionSignatureProgram());
        case ValueKind::FunctionSignature$Parameterize:
            return compare(a.functionSignatureBaseValue(), b.functionSignatureBaseValue());
        case ValueKind::BooleanLiteral:
            return a.booleanValue() <=> b.booleanValue();
        case ValueKind::MemberPointer:
            return compare(program->getMemberPointer(a), program->getMemberPointer(b));
        case ValueKind::Parameterize:
            return program->compareParameterizes(a, b);
        case ValueKind::Expression:
            return a.expressionIndex() <=> b.expressionIndex();
        case ValueKind::RemoteExpression:
            return compare(program->getRemoteExpression(a), program->getRemoteExpression(b));
        case ValueKind::Parameter:
            return a.parameterIndex() <=> b.parameterIndex();
        default:
            VERIFY_NOT_REACHED();
    }
}

std::strong_ordering Util::compare(ProgramHandle a, ProgramHandle b) {
    return get(a)->declarationLocation() <=> get(b)->declarationLocation();
}

std::strong_ordering Util::compare(NamespaceHandle a, NamespaceHandle b) {
    // TODO: This both slow and not very nice since it does not consider nesting
    auto nameOrdering = context.wordTable.view(get(a)->name) <=> context.wordTable.view(get(b)->name);
    if (nameOrdering != 0)
        return nameOrdering;

    // arbirary tie breaker since namespaces with the same name can exist in different parents
    return a.id() <=> b.id();
}

std::strong_ordering Util::compare(Parameterize a, Parameterize b) {
    auto baseOrdering = compare(a.base, b.base);
    if (baseOrdering != 0)
        return baseOrdering;

    if (a.arguments.size() != b.arguments.size()) {
        // One side is a partial parameterization with only inherited arguments and the other is a full parameterization
        //  -> the full parameterization should have lower ordering (higher priority)
        return a.arguments.size() < b.arguments.size() ? std::strong_ordering::greater : std::strong_ordering::less;
    }

    for (int_t i = 0; i < (int_t)a.arguments.size(); i++) {
        auto ordering = compare(a.arguments[i], b.arguments[i]);
        if (ordering != 0)
            return ordering;
    }
    return std::strong_ordering::equal;
}

std::strong_ordering Util::compare(MemberPointer a, MemberPointer b) {
    auto parentTypeOrdering = compare(a.parentType, b.parentType);
    if (parentTypeOrdering != 0)
        return parentTypeOrdering;

    return a.memberIndex <=> b.memberIndex;
}

std::strong_ordering Util::compare(RemoteExpression a, RemoteExpression b) {
    auto baseOrdering = compare(a.base, b.base);
    if (baseOrdering != 0)
        return baseOrdering;

    return Value(a.expression).expressionIndex() <=> Value(b.expression).expressionIndex();
}

}