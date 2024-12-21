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

std::strong_ordering Util::compare(Constant a, Constant b) {
    auto kindOrdering = a.kind() <=> b.kind();
    if (kindOrdering != 0)
        return kindOrdering;

    switch (a.kind()) {
        case ConstantKind::Program:
            return compare(a.program(), b.program());
        case ConstantKind::Namespace:
            return compare(a.nsHandle(), b.nsHandle());
        case ConstantKind::TemplateSignature$Program:
            return compare(a.templateSignatureProgram(), b.templateSignatureProgram());
        case ConstantKind::TemplateSignature$Parameterize:
            return compare(a.templateSignatureBaseConstant(), b.templateSignatureBaseConstant());
        case ConstantKind::FunctionSignature$Program:
            return compare(a.functionSignatureProgram(), b.functionSignatureProgram());
        case ConstantKind::FunctionSignature$Parameterize:
            return compare(a.functionSignatureBaseConstant(), b.functionSignatureBaseConstant());
        case ConstantKind::BooleanLiteral:
            return a.booleanValue() <=> b.booleanValue();
        case ConstantKind::MemberPointer:
            return compare(program->getMemberPointer(a), program->getMemberPointer(b));
        case ConstantKind::Parameterize:
            return program->compareParameterizes(a, b);
        case ConstantKind::Computed:
            return a.id() <=> b.id();
        case ConstantKind::RemoteComputed:
            return compare(program->getRemoteComputedConstant(a), program->getRemoteComputedConstant(b));
        case ConstantKind::CopyOfParameter:
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

std::strong_ordering Util::compare(RemoteComputation a, RemoteComputation b) {
    auto baseOrdering = compare(a.base, b.base);
    if (baseOrdering != 0)
        return baseOrdering;

    return Constant(a.computation).id() <=> Constant(b.computation).id();
}

}