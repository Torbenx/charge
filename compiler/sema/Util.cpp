#include <sema/Util.h>

#include <sema/Context.h>

namespace sema {

Util::Util(Context& context, ProgramHandle programHandle)
    : context(context), program(context.program(programHandle)), programHandle(programHandle) { }

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
        case ConstantKind::ExpressionCategoryLiteral:
            return a.expressionCategory() <=> b.expressionCategory();
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
        case ConstantKind::CopyOfParameterToReferenceCategory:
            return a.originalExpressionCategory().parameterIndex() <=> b.originalExpressionCategory().parameterIndex();
        case ConstantKind::CopyOfOpenGlobal$Program:
            return compare(a.copiedGlobal().program(), b.copiedGlobal().program());
        case ConstantKind::CopyOfOpenGlobal$Parameterize:
            return program->compareParameterizes(a.copiedGlobal(), b.copiedGlobal());
        case ConstantKind::OpenReturnType$Parameterize:
            return program->compareParameterizes(a.returnTypeOf(), b.returnTypeOf());
        case ConstantKind::EnumValue:
            return compare(program->getEnumValue(a), program->getEnumValue(b));
        case ConstantKind::CopyOfError:
            return a.id() <=> b.id();
        default:
            VERIFY_NOT_REACHED();
    }
}

std::strong_ordering Util::compare(ProgramHandle a, ProgramHandle b) {
    ModuleHandle m = context.moduleOf(programHandle);
    return context.program(context.translate(m, a))->declarationLocation() <=> context.program(context.translate(m, b))->declarationLocation();
}

std::strong_ordering Util::compare(NamespaceHandle a, NamespaceHandle b) {
    // TODO: This both slow and not very nice since it does not consider nesting
    auto getName = [this, m = context.moduleOf(programHandle)](NamespaceHandle nsHandle) {
        return context.tokenBuffer.wordTable.view(context.getNamespace(context.translate(m, nsHandle))->name);
    };
    auto nameOrdering = getName(a) <=> getName(b);
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
    auto lengthOrdering = a.elements.size() <=> b.elements.size();
    if (lengthOrdering != 0)
        return lengthOrdering;

    for (int_t i = 0; i < (int_t)a.elements.size(); i++) {
        auto implOrdering = compare(a.elements[i].structImpl, b.elements[i].structImpl);
        if (implOrdering != 0)
            return implOrdering;

        auto memberIndexOrdering = a.elements[i].memberIndex <=> b.elements[i].memberIndex;
        if (memberIndexOrdering != 0)
            return memberIndexOrdering;
    }
    return std::strong_ordering::equal;
}

std::strong_ordering Util::compare(RemoteComputation a, RemoteComputation b) {
    auto baseOrdering = compare(a.base, b.base);
    if (baseOrdering != 0)
        return baseOrdering;

    return Constant(a.computation).id() <=> Constant(b.computation).id();
}

std::strong_ordering Util::compare(EnumValue a, EnumValue b) {
    auto typeOrdering = compare((Constant)a.enumType, (Constant)b.enumType);
    if (typeOrdering != 0)
        return typeOrdering;

    return a.valueIndex <=> b.valueIndex;
}

FoldBase Util::asFoldBase(Constant base) {
    return tryAsFoldBase(base).value();
}

std::optional<FoldBase> Util::tryAsFoldBase(Constant base) {
    if (base.kind() == ConstantKind::Program) {
        Program* baseProg = context.program(base.program());
        if (baseProg->isDependent())
            return std::nullopt;
        return FoldBase { baseProg, context.moduleOf(base.program()), base.program(), base, {} };
    } else if (base.kind() == ConstantKind::Parameterize) {
        auto param = program->getParameterize(base);
        Program* baseProg = context.program(param.base);
        if (baseProg->parameters.size() != param.arguments.size())
            return std::nullopt;
        return FoldBase { context.program(param.base), context.moduleOf(param.base), param.base, base, param.arguments };
    }
    return std::nullopt;
}

}