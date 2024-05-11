#include <sema/Context.h>
#include <sema/Generator.h>

namespace sema {

Expression Generator::topExpression(int_t n) {
    return topNode(n);
}

Node* Generator::topNode(int_t n) {
    return &nodeScratch[(nodeStack.end() - n - 1)->nodeIndex];
}

void Generator::popNode() {
    int_t size = nodeScratch.back().subTreeSize();
    for (int_t i = 0; i < size; i++)
        nodeScratch.pop_back();
    nodeStack.pop_back();
}

void Generator::popNodes(int_t n) {
    for (int_t i = 0; i < n; i++)
        popNode();
}

void Generator::emitNode(NodeKind kind, SourceLocation location, int_t childCount, NodeData data) {
    int_t subTreeSize = 1;
    for (int_t i = 0; i < childCount; i++) {
        subTreeSize += nodeScratch[nodeStack.back().nodeIndex].subTreeSize();
        nodeStack.pop_back();
    }
    int_t index = nodeScratch.size();
    nodeScratch.emplace_back(kind, location, childCount, subTreeSize, data);
    nodeStack.push_back({ (uint32_t)index });
}

void Generator::emitExpr(NodeKind kind, SourceLocation location, int_t childCount, Type type, ExprData data) {
    emitNode(kind, location, childCount, NodeData { .expr = { type, data } });
}

void Generator::emitConstantExpr(SourceLocation location, Value value) {
    emitExpr(NodeKind::ConstantExpr, location, 0, typeOf(value), ExprData { .constant = value });
}

Value Generator::makeExpressionValue() {
    Value value = makeExpressionValue(topExpression());
    popNode();
    return value;
}

Value Generator::makeExpressionValue(Expression expr) {
    if (expr.kind() == NodeKind::ConstantExpr)
        return expr.data().constant;
    else
        return program->addExpression(expr);
}

Value Generator::makeTemplateSignature(ProgramHandle progHandle) {
    return Value(ValueKind::TemplateSignature, progHandle.id());
}

Value Generator::makeFunctionSignature(Value value) {
    if (value.kind() == ValueKind::Program) {
        VERIFY(!context.program(value.program())->isDependent());
        return Value(ValueKind::FunctionSignature$Program, value.id());
    }
    if (value.kind() == ValueKind::Parameterize) {
        return Value(ValueKind::FunctionSignature$Parameterize, value.id());
    }
    VERIFY_NOT_REACHED();
}

Type Generator::makeTemplateIdFor(ProgramHandle targetHandle) {
    Program* targetProg = context.program(targetHandle);
    VERIFY(targetProg->isTemplate());
    std::array arguments { makeTemplateSignature(targetHandle) };
    return verifyType(program->addParameterize(builtins::template_id_template.program(), arguments));
}

Value Generator::makeProgramValue(ProgramHandle targetHandle) {
    Program* targetProg = context.program(targetHandle);
    if (targetProg->isDependent() && !targetProg->isTemplate()) {
        auto parent = program->getParameterize((Value)program->parent());
        return program->addParameterize(targetHandle, parent.arguments.subspan(0, targetProg->inheritedParameterCount));
    }
    return Value(targetHandle);
}

Value Generator::makeParameterize(ProgramHandle base, std::span<const Value> arguments) {
    if (arguments.empty())
        return Value(base);
    return program->addParameterize(base, arguments);
}

Value Generator::buildParent(Value rawParent) {
    if (rawParent.kind() == ValueKind::Namespace)
        return rawParent;

    VERIFY(rawParent.kind() == ValueKind::Program);

    ProgramHandle parentHandle = rawParent.program();
    signatureCheck(context, parentHandle);
    Program* parentProg = context.program(parentHandle);
    if (!parentProg->isDependent())
        return makeProgramValue(parentHandle);

    // inherite parameters
    int_t parameterCount = parentProg->parameters.size();
    VERIFY(parameterCount > 0);
    std::vector<Value> arguments(parameterCount, INVALID_VALUE);
    for (int_t i = 0; i < parameterCount; i++)
        arguments[i] = addInheritedParameter(Type(), std::nullopt);

    Value parentValue = program->addParameterize(parentHandle, arguments);
    FoldBase base = asFoldBase(parentValue);
    for (int_t i = 0; i < parameterCount; i++) {
        Type type = verifyType(fold(base, base.program->parameters[i].type));
        program->parameters[i].type = type;
    }
    return parentValue;
}

Value Generator::buildSelf() {
    int_t parameterCount = program->parameters.size();
    std::vector<Value> arguments(parameterCount, INVALID_VALUE);
    for (int_t i = 0; i < parameterCount; i++)
        arguments[i] = Value(ValueKind::Parameter, i);
    return makeParameterize(programHandle, arguments);
}

std::optional<Value> Generator::lookupInside(Value scope, Word name) {
    auto rawToValue = [this](Value rawValue) { return generateDeclarationLiteral(rawValue); };
    if (scope.kind() == ValueKind::Namespace) {
        Namespace* ns = context.getNamespace(scope.nsHandle());
        return ns->getDeclaration(name).transform(rawToValue);
    }
    if (scope.kind() == ValueKind::Program) {
        Program* prog = context.program(scope.program());
        VERIFY(prog->kind() == ProgramKind::Type);
        return static_cast<TypeProgram*>(prog)->getDeclaration(name).transform(rawToValue);
    }
    if (scope.kind() == ValueKind::Parameterize) {
        auto param = program->getParameterize(scope);
        Program* baseProg = context.program(param.base);
        auto lookup = static_cast<TypeProgram*>(baseProg)->getDeclaration(name);
        if (!lookup.has_value())
            return std::nullopt;
        // 'baseProg' must be a parent of 'program'
        VERIFY(program->parent().kind() == ValueKind::Parameterize);
        VERIFY(program->inheritedParameterCount >= param.arguments.size());
        return lookup.transform(rawToValue);
    }
    VERIFY_NOT_REACHED();
}

Value Generator::generateDeclarationLiteral(Value rawValue) {
    if (rawValue.kind() == ValueKind::Namespace)
        return rawValue;
    VERIFY(rawValue.kind() == ValueKind::Program);
    ProgramHandle progHandle = rawValue.program();
    Program* prog = context.program(progHandle);
    signatureCheck(context, progHandle);
    switch (prog->kind()) {
    case ProgramKind::Type:
    case ProgramKind::Function:
        return makeProgramValue(progHandle);
    case ProgramKind::Value: {
        Value progValue = makeProgramValue(progHandle);
        if (prog->isTemplate())
            return progValue;
        return fold(progValue, static_cast<ValueProgram*>(prog)->value());
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

void Generator::generateIdentifierExpr() {
    Word name = Word::fromUint(tok->data());
    /*for (const auto& entry : localDeclarations) {
        if (name == entry.name) {
            emitValueExpr({ NodeKind::ReferenceExpr, tok->location() }, entry.value);
            return;
        }
    }*/
    auto result = lookupCache.get(name);
    if (result.has_value()) {
        emitConstantExpr(tok->location(), result.value());
        return;
    }
    std::optional<Value> lookupScope = (Value)program->parent();
    while (lookupScope.has_value()) {
        auto lookup = lookupInside(lookupScope.value(), name);
        if (lookup.has_value()) {
            lookupCache.insert(name, lookup.value());
            emitConstantExpr(tok->location(), lookup.value());
            return;
        }
        switch (lookupScope.value().kind()) {
        case ValueKind::Namespace:
            lookupScope = context.getNamespace(lookupScope.value().nsHandle())->parent.transform([](NamespaceHandle h) { return (Value)h; });
            break;
        case ValueKind::Program:
            lookupScope = (Value)context.program(lookupScope.value().program())->parent();
            break;
        case ValueKind::Parameterize:
            lookupScope = (Value)context.program(program->getParameterize(lookupScope.value()).base)->parent();
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateParameterizeExpr(int_t argumentCount) {
    Expression baseExpr = topExpression(argumentCount);
    if (baseExpr.kind() == NodeKind::ConstantExpr) {
        Value baseValue = baseExpr.data().constant;
        if (baseValue.kind() == ValueKind::Program) {
            Program* baseProg = context.program(baseValue.program());
            VERIFY(baseProg->isTemplate());
            int_t parameterCount = baseProg->parameters.size();
            DeductionState state(baseProg, baseValue.program(), parameterCount);
            state.identityMap(baseProg->inheritedParameterCount);

            int_t pIndex = baseProg->inheritedParameterCount;
            int_t aIndex = 0;
            for (; aIndex < argumentCount; aIndex++, pIndex++) {
                // Find next explicit parameter
                while (pIndex < parameterCount && baseProg->parameters[pIndex].implicit())
                    pIndex += 1;
                VERIFY(pIndex < parameterCount);

                ExternValue pType = baseProg->parameters[pIndex].type;
                Expression argument = topExpression(argumentCount - 1 - aIndex);
                implicitCastTo(state, pType, argument);
                state.explicitArgument(pIndex, makeExpressionValue(argument));
            }
            VERIFY(pIndex == parameterCount);
            VERIFY(state.isComplete());
            popNodes(argumentCount + 1);
            Value result = makeParameterize(baseValue.program(), state.arguments);
            emitConstantExpr({}, result);
            return;
        }
    }
    VERIFY_NOT_REACHED();
}

Generator::CallBase Generator::resolveCallBase() {
    auto baseExpr = topExpression();
    // TODO: Allow only function and type programs
    if (baseExpr.kind() == NodeKind::ConstantExpr) {
        auto baseValue = baseExpr.data().constant;
        if (baseValue.kind() == ValueKind::Program) {
            Program* baseProg = context.program(baseValue.program());
            DeductionState state(baseProg, baseValue.program(), baseProg->parameters.size());
            state.identityMap(baseProg->inheritedParameterCount);
            popNode();
            return { std::move(state) };
        } else if (baseValue.kind() == ValueKind::Parameterize) {
            auto param = program->getParameterize(baseValue);
            Program* baseProg = context.program(param.base);
            VERIFY(baseProg->parameters.size() == param.arguments.size());
            DeductionState state(baseProg, param.base, param.arguments.size());
            for (int_t i = 0; i < (int_t)param.arguments.size(); i++)
                state.explicitArgument(i, param.arguments[i]);
            popNode();
            return { std::move(state) };
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateCallExpr(CallBase base, int_t argumentCount) {
    auto& state = base.state;
    if (state.program->kind() == ProgramKind::Function) {
        FunctionProgram* fnProg = static_cast<FunctionProgram*>(state.program);

        VERIFY(argumentCount == (int_t)fnProg->runtimeParameters.size());
        for (int_t index = 0; index < argumentCount; index++) {
            const auto& parameter = fnProg->runtimeParameters[index];
            Expression argument = topExpression(argumentCount - 1 - index);
            implicitCastTo(state, parameter.type(), argument);
        }
        VERIFY(base.state.isComplete());
        Value callBase = makeParameterize(state.programHandle, state.arguments);
        Type returnType = verifyType(fold(callBase, fnProg->type()));
        emitExpr(NodeKind::CallExpr, SourceLocation(), argumentCount, returnType, ExprData { .callBase = callBase });
        return;
    }
    VERIFY_NOT_REACHED();
}

void Generator::implicitToType() {
    Type type = verifyType(makeExpressionValue());
    emitConstantExpr({}, type);
}

Value Generator::implicitCastTo(DeductionState& state, ExternValue pType, Expression arg) {
    bool sameType = staticMatch(state, pType, arg.type());
    VERIFY(sameType);
    return makeExpressionValue(arg);
}

void Generator::implicitCastTo(DeductionState& state, ExternValue pType) {
    bool sameType = staticMatch(state, pType, topExpression().type());
    VERIFY(sameType);
}

FoldBase Generator::asFoldBase(Value base) {
    return tryAsFoldBase(base).value();
}

std::optional<FoldBase> Generator::tryAsFoldBase(Value base) {
    if (base.kind() == ValueKind::Program) {
        Program* baseProg = context.program(base.program());
        if (baseProg->isDependent())
            return std::nullopt;
        return FoldBase { baseProg, base.program(), base, {} };
    } else if (base.kind() == ValueKind::Parameterize) {
        auto param = program->getParameterize(base);
        return FoldBase { context.program(param.base), param.base, base, param.arguments };
    }
    return std::nullopt;
}

FoldBase Generator::selfFold() {
    return asFoldBase((Value)program->self());
}

DeductionState Generator::selfDeduction() {
    DeductionState state(program, programHandle, program->parameters.size());
    state.identityMap(program->parameters.size());
    return state;
}

// pValue and aValue must be known to have the same type
bool Generator::staticMatch(DeductionState& state, ExternValue pValue, Value aValue) {
    if (pValue.kind() == ValueKind::Parameter) {
        int_t index = pValue.id();
        if (state.arguments[index] == INVALID_VALUE) {
            state.arguments[index] = aValue;
            VERIFY(!state.isExplicitArgument(index));
            return true;
        }
        auto selfState = selfDeduction();
        bool result = staticMatch(selfState, state.arguments[index], aValue);
        state.expressionMatches.insert(state.expressionMatches.end(), selfState.expressionMatches.begin(), selfState.expressionMatches.end());
        return result;
    }

    if (pValue.kind() == ValueKind::Expression || pValue.kind() == ValueKind::RemoteExpression) {
        // TODO: check that the expression does not contain any deduced arguments
        state.expressionMatches.push_back({ pValue, aValue });
        return true;
    }
    if (aValue.kind() == ValueKind::Expression || aValue.kind() == ValueKind::RemoteExpression) {
        state.expressionMatches.push_back({ pValue, aValue });
        return true;
    }

    auto comparePrograms = [&state](ProgramHandle pProg, ProgramHandle aProg) {
        return state.program->translate(pProg) == aProg;
    };

    if (pValue.kind() != aValue.kind())
        return false;
    switch (pValue.kind()) {
    case ValueKind::Program:
        return comparePrograms(Value(pValue).program(), aValue.program());
    case ValueKind::Namespace:
        return state.program->translate(Value(pValue).nsHandle()) == aValue.nsHandle();
    case ValueKind::TemplateSignature:
        return comparePrograms(Value(pValue).templateSignatureProgram(), aValue.templateSignatureProgram()); // TODO: different programs can have the same signature
    case ValueKind::FunctionSignature$Program:
        return comparePrograms(Value(pValue).functionSignatureProgram(), aValue.functionSignatureProgram()); // TODO: different programs can have the same signature
    case ValueKind::FunctionSignature$Parameterize:
        // TODO: different programs can have the same signature
        pValue = Value(pValue).functionSignatureBaseValue();
        aValue = aValue.functionSignatureBaseValue();
        [[fallthrough]];
    case ValueKind::Parameterize: {
        auto pPara = state.program->getParameterize(pValue);
        auto aPara = program->getParameterize(aValue);
        if (!comparePrograms(pPara.base, aPara.base))
            return false;
        VERIFY(pPara.arguments.size() == aPara.arguments.size());
        for (int_t i = 0; i < (int_t)pPara.arguments.size(); i++) {
            if (!staticMatch(state, pPara.arguments[i], aPara.arguments[i]))
                return false;
        }
        return true;
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Value Generator::fold(Value base, ExternValue v) {
    return fold(asFoldBase(base), v);
}

Value Generator::fold(FoldBase base, ExternValue v) {
    auto foldProgram = [&base](ProgramHandle handle) {
        return base.program->translate(handle);
    };
    switch (v.kind()) {
    case ValueKind::Program:
        return makeProgramValue(foldProgram(Value(v).program()));
    case ValueKind::Parameter:
        return base.arguments[v.id()];
    case ValueKind::Namespace:
        return (Value)base.program->translate(Value(v).nsHandle());
    case ValueKind::TemplateSignature:
        return makeTemplateSignature(foldProgram(Value(v).templateSignatureProgram()));
    case ValueKind::FunctionSignature$Program:
    case ValueKind::FunctionSignature$Parameterize:
        return makeFunctionSignature(fold(base, Value(v).functionSignatureBaseValue()));
    case ValueKind::RemoteExpression: {
        RemoteExpression expr = base.program->getRemoteExpression(v);
        return program->addRemoteExpression(fold(base, expr.base), expr.expressionIndex);
    }
    case ValueKind::Expression:
        return program->addRemoteExpression(base.value, Value(v).expressionIndex());
    case ValueKind::Parameterize: {
        auto externPara = base.program->getParameterize(v);
        std::vector<Value> foldedArgs;
        for (auto arg : externPara.arguments)
            foldedArgs.push_back(fold(base, arg));
        return program->addParameterize(foldProgram(externPara.base), foldedArgs);
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

void Generator::signatureCheck(Context& context, ProgramHandle progHandle) {
    Program* program = context.program(progHandle);
    if (program->status() >= ProgramStatus::SignatureChecked)
        return;
    VERIFY(program->status() == ProgramStatus::Unchecked);

    Generator g(context, progHandle);
    Value parent = g.buildParent(program->rawParent());
    auto parseLocation = program->beginSignatureCheck(parent);

    g.tok = &context.parseOutput.tokens[parseLocation.id()];
    g.visitDeclaration();
    g.tok = nullptr;

    program->completeSignatureCheck(g.buildSelf());
}

Type Generator::typeOf(Value value) {
    switch (value.kind()) {
    case ValueKind::TemplateSignature:
        return builtins::template_signature_type;
    case ValueKind::FunctionSignature$Program:
    case ValueKind::FunctionSignature$Parameterize:
        return builtins::function_signature_type;
    case ValueKind::Namespace:
        return builtins::namespace_type;
    case ValueKind::Expression:
        return Expression(&program->expressions[value.expressionIndex()]).type();
    case ValueKind::RemoteExpression: {
        auto rExpr = program->getRemoteExpression(value);
        auto base = asFoldBase(rExpr.base);
        return verifyType(fold(base, Expression(&base.program->expressions[rExpr.expressionIndex]).type()));
    }
    case ValueKind::Parameterize:
        return typeOfNonDependentProgram(value);
    case ValueKind::Parameter:
        return parameterTypes[value.id()];
    case ValueKind::Program: {
        Program* valueProg = context.program(value.program());
        if (!valueProg->isDependent())
            return typeOfNonDependentProgram(value);
        return makeTemplateIdFor(value.program());
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Type Generator::typeOfNonDependentProgram(Value value) {
    return typeOfNonDependentProgram(asFoldBase(value));
}

Type Generator::typeOfNonDependentProgram(FoldBase base) {
    if (base.program->kind() == ProgramKind::Function) {
        std::array arguments { makeFunctionSignature(base.value) };
        return verifyType(program->addParameterize(builtins::function_id_template.program(), arguments));
    }
    return verifyType(fold(base, base.program->type()));
}

Type Generator::verifyType(Value value) {
    switch (value.kind()) {
    case ValueKind::Program: {
        Program* valueProg = context.program(value.program());
        VERIFY(!valueProg->isTemplate());
        VERIFY(valueProg->kind() != ProgramKind::Function);
        VERIFY(valueProg->type() == builtins::type_type);
        return (Type)value;
    }
    default:
        VERIFY(typeOf(value) == builtins::type_type);
        return (Type)value;
    }
}

Value Generator::addParameter(Word name, Type type, std::optional<Value> defaultValue) {
    VERIFY(parameterTypes.size() == program->parameters.size());
    uint32_t parameterIndex = program->parameters.size();
    parameterTypes.push_back(type);
    program->parameters.push_back({ name, type, defaultValue });
    return Value(ValueKind::Parameter, parameterIndex);
}

Value Generator::addExplicitParameter(Word name, Type type, std::optional<Value> defaultValue) {
    return addParameter(name, type, defaultValue);
}

Value Generator::newImplicitParameter(Type type) {
    uint32_t parameterIndex = parameterTypes.size();
    parameterTypes.push_back(type);
    return Value(ValueKind::Parameter, parameterIndex);
}

Value Generator::addInheritedParameter(Type type, std::optional<Value> defaultValue) {
    VERIFY(program->parameters.size() == program->inheritedParameterCount);
    program->inheritedParameterCount += 1;
    return addParameter(Word(), type, defaultValue);
}

struct BuiltinGenerator : Generator {
    static Word nameOf(BuiltinId id) {
        switch (id) {
#define BUILTIN(name, id) \
    case BuiltinId::id:   \
        return parse::words[#name];
#include <sema/builtins.inc>

        default:
            VERIFY_NOT_REACHED();
        }
    }

    static ProgramHandle createScope(Context& context, BuiltinId id) {
        auto value = context.pushStaticScope(ProgramKind::Type, nameOf(id), {}, {});
        VERIFY(value == Value(id));
        return value.program();
    }

    BuiltinGenerator(Context& context, BuiltinId id)
        : Generator(context, createScope(context, id)) {
        program->beginSignatureCheck(buildParent(program->rawParent()));
    }

    ~BuiltinGenerator() {
        program->completeSignatureCheck(buildSelf());
        context.popScope();
    }
};

void Generator::generateBuiltins(Context& context) {
    {
        BuiltinGenerator g { context, BuiltinId::type_type };
        g.program->setType(builtins::type_type);
    }
    {
        BuiltinGenerator g { context, BuiltinId::error_type };
        g.program->setType(builtins::type_type);
    }
    {
        BuiltinGenerator g { context, BuiltinId::namespace_type };
        g.program->setType(builtins::type_type);
    }

    // typeof(tempalte(T: type) => expr) = template_id{template(T: type) -> typeof(expr)}
    // cast{type}(template(T: type) -> type_expr) = template_id{template(T: type) -> type_expr}

    // template(sig: template_signature) struct template_id: { }
    // typof(template_id) = typeof(template(sig: template_signature) => template_id{sig})
    //                    = template_id{template(sig: template_signature) -> typeof(template_id{sig})}
    //                    = template_id{template(sig: template_signature) -> type}
    {
        BuiltinGenerator g { context, BuiltinId::template_signature_type };
        g.program->setType(builtins::type_type);
    }
    {
        BuiltinGenerator g { context, BuiltinId::template_id_template };
        g.addExplicitParameter(parse::words["sig"], builtins::template_signature_type, {});
        g.program->setType(builtins::type_type);
    }

    // template(sig: function_signature) struct function_id: { }
    // typeof(function_id) = typeof(template(sig: function_signature) => function_id{sig})
    //                     = template_id{template(sig: function_signature) -> typeof(function_id{sig})}
    //                     = template_id{template(sig: function_signature) -> type}
    {
        BuiltinGenerator g { context, BuiltinId::function_signature_type };
        g.program->setType(builtins::type_type);
    }
    {
        BuiltinGenerator g { context, BuiltinId::function_id_template };
        g.addExplicitParameter(parse::words["sig"], builtins::function_signature_type, {});
        g.program->setType(builtins::type_type);
    }

    // cast{template_id}( template_function_id{template(T: type) fn(t: T) -> T)} )
    //   = template_id{ template(T: type) -> function_id{fn(t: T) -> T} }

    // template(T: type) fn(t: T) -> T = template(T: type) -> function_id{fn(t: T) -> T}

    // typeof( (arg = expr) ) = cast{type}( (arg = typeof(expr)) ) = tuple{cast{tuple_signature}( (arg = typeof(expr)) )}
}

}