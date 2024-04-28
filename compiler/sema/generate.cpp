#include <glue/Context.h>
#include <sema/Generator.h>

namespace sema {

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
    emitExpr(NodeKind::ConstantExpr, location, 0, typeOf(value), ExprData { .value = value });
}

Value Generator::makeExpressionValue() {
    Value value = makeExpressionValue(topExpression());
    popExpression();
    return value;
}

Value Generator::makeExpressionValue(Expression expr) {
    if (expr.kind() == NodeKind::ConstantExpr)
        return expr.data().value;
    else
        return program->addExpression(expr);
}

Type Generator::makeTemplateIdFor(ProgramHandle targetHandle) {
    Program* targetProg = &context.programs[targetHandle.id()];
    VERIFY(targetProg->isTemplate());
    std::array arguments { program->addTemplateSignatureOf(builtins::template_signature_type, targetHandle) };
    return verifyType(program->addParameterize(
        builtins::type_type, builtins::template_id_template.program(), arguments));
}

Value Generator::makeProgramValue(ProgramHandle targetHandle) {
    Program* targetProg = &context.programs[targetHandle.id()];
    if (targetProg->isDependent() && !targetProg->isTemplate()) {
        Value result = program->addParameterize(Type(), targetHandle, 0, targetProg->inheritedParameterCount);
        program->constants[result.id()].type = verifyType(fold(result, targetProg->type()));
        return result;
    }
    return Value(ValueKind::Program, targetHandle.id());
}

void Generator::buildParent(glue::DeclarationNode* parentNode) {
    if (parentNode->kind() == glue::DeclarationNode::Kind::Namespace) {
        program->setParent(program->addNamespaceLiteral(parentNode));
        return;
    }

    ProgramHandle parentHandle = signatureCheck(context, parentNode);
    Program* parentProg = &context.programs[parentHandle.id()];
    if (!parentProg->isDependent()) {
        program->setParent(makeProgramValue(parentHandle));
        return;
    }

    // inherite parameters
    int_t parameterCount = parentProg->parameters.size();
    VERIFY(parameterCount > 0);
    std::vector<Value> arguments(parameterCount, INVALID_VALUE);
    for (int_t i = 0; i < parameterCount; i++)
        arguments[i] = addInheritedParameter(Type(), std::nullopt);

    Value parentValue = program->addParameterize(Type(), parentHandle, arguments);
    FoldState base = asFoldBase(parentValue);
    for (int_t i = 0; i < parameterCount; i++) {
        Type type = verifyType(fold(base, base.program->parameters[i].type));
        program->parameters[i].type = type;
    }
    VERIFY(parentValue.kind() == ValueKind::Constant);
    program->constants[parentValue.id()].type = verifyType(fold(base, base.program->type()));
    program->setParent(parentValue);
}

void Generator::buildSelf() {
    if (!program->isDependent()) {
        program->setSelf(makeProgramValue(programHandle));
        return;
    }

    int_t parameterCount = program->parameters.size();
    std::vector<Value> arguments(parameterCount, INVALID_VALUE);
    for (int_t i = 0; i < parameterCount; i++)
        arguments[i] = Value(ValueKind::Parameter, i);
    Type type = verifyType((Value)program->type());
    program->setSelf(program->addParameterize(type, programHandle, arguments));
}

std::optional<Value> Generator::lookupInScope(glue::DeclarationNode* scope, Word name) {
    using Kind = glue::DeclarationNode::Kind;
    if (scope->kind() == Kind::Namespace || scope->kind() == Kind::Type) {
        if (scope->kind() == Kind::Type) {
            VERIFY(scope->program().has_value());
            VERIFY(context.programs[scope->program().value().id()].status() >= ProgramStatus::SignatureChecked);
        }
        auto child = scope->findChild(name);
        if (child.has_value())
            return generateDeclarationLiteral(child);
        return std::nullopt;
    }
    return std::nullopt;
}

Value Generator::generateDeclarationLiteral(glue::DeclarationNode* target) {
    using Kind = glue::DeclarationNode::Kind;
    switch (target->kind()) {
    case Kind::Namespace:
        return program->addNamespaceLiteral(target);
    case Kind::Type:
    case Kind::Function: {
        return makeProgramValue(signatureCheck(context, target));
    }
    case Kind::Variable: {
        Value progValue = makeProgramValue(signatureCheck(context, target));
        if (auto maybe = getProgramLiteral(progValue); maybe.has_value())
            return fold(progValue, maybe->value());
        return progValue;
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
    glue::DeclarationNode* lookupScope = currentScope->declaringNode();
    while (lookupScope != nullptr) {
        auto lookup = lookupInScope(lookupScope, name);
        if (lookup.has_value()) {
            lookupCache.insert(name, lookup.value());
            emitConstantExpr(tok->location(), lookup.value());
            return;
        }
        lookupScope = lookupScope->declaringNode();
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateParameterizeExpr(int_t argumentCount) {
    Expression baseExpr = topExpression(argumentCount);
    if (baseExpr.kind() == NodeKind::ConstantExpr) {
        Value baseValue = baseExpr.data().value;
        if (baseValue.kind() == ValueKind::Program) {
            Program* baseProg = &context.programs[baseValue.id()];
            VERIFY(baseProg->isTemplate());
            int_t parameterCount = baseProg->parameters.size();
            DeductionState state(baseProg, parameterCount);
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
                if (argument.kind() == NodeKind::ConstantExpr) {
                    state.explicitArgument(pIndex, argument.data().value);
                } else {
                    VERIFY_NOT_REACHED();
                }
            }
            VERIFY(state.isComplete());
            popExpressions(argumentCount + 1);
            Value result = program->addParameterize(Type(), baseValue.program(), state.arguments);
            Type type = verifyType(fold(result, baseProg->type()));
            program->constants[result.id()].type = type;
            emitConstantExpr({}, result);
            return;
        }
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

FoldState Generator::asFoldBase(Value base) {
    if (base.kind() == ValueKind::Program) {
        Program* baseProg = &context.programs[base.id()];
        VERIFY(!baseProg->isDependent());
        return { baseProg, base, {} };
    }
    VERIFY(base.kind() == ValueKind::Constant);

    const auto& baseConst = program->constants[base.id()];
    VERIFY(baseConst.op == Program::Opcode::Parameterize);
    Program* baseProg = &context.programs[baseConst.u.parameterize.base.id()];
    return { baseProg, base, parameterizeArguments(base) };
}

FoldState Generator::selfFold() {
    return asFoldBase((Value)program->self());
}

DeductionState Generator::selfDeduction() {
    DeductionState state(program, program->parameters.size());
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

    if (pValue.kind() == ValueKind::Constant) {
        const auto& pConst = state.program->constants[pValue.id()];
        if (pConst.op == Program::Opcode::Expression || pConst.op == Program::Opcode::RemoteExpression) {
            // TODO: check that the expression does not contain any deduced arguments
            state.expressionMatches.push_back({ pValue, aValue });
            return true;
        }
    }
    if (aValue.kind() == ValueKind::Constant) {
        const auto& aConst = program->constants[aValue.id()];
        if (aConst.op == Program::Opcode::Expression || aConst.op == Program::Opcode::RemoteExpression) {
            state.expressionMatches.push_back({ pValue, aValue });
            return true;
        }
    }

    if (pValue.kind() != aValue.kind())
        return false;
    if (pValue.kind() == ValueKind::Program)
        return state.program->programTranslationBuffer[pValue.id()] == aValue.program();
    VERIFY(pValue.kind() == ValueKind::Constant);
    const auto& pConst = state.program->constants[pValue.id()];
    const auto& aConst = program->constants[aValue.id()];

    if (pConst.op != aConst.op)
        return false;
    switch (pConst.op) {
    case Program::Opcode::NamespaceLiteral:
        return pConst.u.declarationNode == aConst.u.declarationNode;
    case Program::Opcode::TemplateFunctionSignatureOf:
        return pConst.u.signatureProgram == aConst.u.signatureProgram; // TODO: different programs can have the same signature
    case Program::Opcode::FunctionSignatureOf:
        return staticMatch(state, pConst.u.signatureValue, aConst.u.signatureValue); // TODO: different programs can have the same signature
    case Program::Opcode::Parameterize: {
        const auto& pPara = pConst.u.parameterize;
        const auto& aPara = aConst.u.parameterize;
        if (pPara.base != aPara.base)
            return false;
        VERIFY(pPara.argumentCount == aPara.argumentCount);
        for (int_t i = 0; i < (int_t)pPara.argumentCount; i++) {
            ExternValue pArgument = state.program->parameterizeArguments[pPara.firstArgumentIndex + i];
            Value aArgument = program->parameterizeArguments[aPara.firstArgumentIndex + i];
            if (!staticMatch(state, pArgument, aArgument))
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

Value Generator::fold(FoldState state, ExternValue v) {
    if (v.kind() == ValueKind::Program)
        return makeProgramValue(state.program->programTranslationBuffer[v.id()]);
    if (v.kind() == ValueKind::Parameter)
        return state.arguments[v.id()];
    VERIFY(v.kind() == ValueKind::Constant);

    const auto& vConst = state.program->constants[v.id()];

    auto type = [&]() -> Type {
        return verifyType(fold(state, vConst.type));
    };

    switch (vConst.op) {
    case Program::Opcode::NamespaceLiteral:
        return program->addNamespaceLiteral(vConst.u.declarationNode);
    case Program::Opcode::TemplateSignatureOf:
        return program->addTemplateSignatureOf(type(), vConst.u.signatureProgram);
    case Program::Opcode::FunctionSignatureOf:
        return program->addFunctionSignatureOf(type(), fold(state, vConst.u.signatureValue));
    case Program::Opcode::RemoteExpression: {
        Value exprBase = fold(state, vConst.u.remoteExpression.base);
        return program->addRemoteExpression(type(), exprBase, vConst.u.remoteExpression.expressionIndex);
    }
    case Program::Opcode::Expression:
        return program->addRemoteExpression(type(), state.value, vConst.u.expressionIndex);
    case Program::Opcode::Parameterize: {
        auto externArgs = parameterizeArguments(state.program, v);
        std::vector<Value> foldedArgs(externArgs.size(), INVALID_VALUE);
        for (int_t argIndex = 0; argIndex < (int_t)externArgs.size(); argIndex++)
            foldedArgs[argIndex] = fold(state, externArgs[argIndex]);
        return program->addParameterize(type(), vConst.u.parameterize.base, foldedArgs);
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

ProgramHandle Generator::signatureCheck(glue::Context& context, glue::DeclarationNode* scope) {
    if (scope->program().has_value()) {
        Program* prog = &context.programs[scope->program()->id()];
        if (prog->status() >= ProgramStatus::SignatureChecked)
            return scope->program().value();
        VERIFY(prog->status() == ProgramStatus::Unchecked);
    } else {
        scope->setProgram(context.newProgram());
    }
    context.programs[scope->program().value().id()].m_name = scope->name();

    Generator g(context, scope);
    g.program->setStatus(ProgramStatus::SignatureCheckInProgress);
    g.buildParent(scope->declaringNode());
    g.visitDeclaration();
    g.buildSelf();
    g.program->setStatus(ProgramStatus::SignatureChecked);
    return g.programHandle;
}

std::optional<Program*> Generator::getProgramLiteral(Value value) {
    if (value.kind() == ValueKind::Program)
        return &context.programs[value.id()];
    const auto& c = program->constants[value.id()];
    if (c.op == Program::Opcode::Parameterize)
        return &context.programs[c.u.parameterize.base.id()];
    return std::nullopt;
}

std::span<Value> Generator::parameterizeArguments(Value value) {
    VERIFY(value.kind() == ValueKind::Constant);
    const auto& param = program->constants[value.id()].u.parameterize;
    return std::span<Value>(&program->parameterizeArguments[param.firstArgumentIndex], param.argumentCount);
}

std::span<const ExternValue> Generator::parameterizeArguments(Program* targetProg, ExternValue value) {
    VERIFY(value.kind() == ValueKind::Constant);
    const auto& param = targetProg->constants[value.id()].u.parameterize;
    return std::span<const ExternValue>(reinterpret_cast<const ExternValue*>(&targetProg->parameterizeArguments[param.firstArgumentIndex]), param.argumentCount);
}

Type Generator::typeOf(Value value) {
    switch (value.kind()) {
    case ValueKind::Constant:
        return program->constants[value.id()].type;
    case ValueKind::Parameter:
        return parameterTypes[value.id()];
    case ValueKind::Program: {
        Program* valueProg = &context.programs[value.id()];
        if (!valueProg->isDependent())
            return verifyType(fold(FoldState { valueProg, value, {} }, valueProg->type()));
        return makeTemplateIdFor(value.program());
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Type Generator::verifyType(Value value) {
    switch (value.kind()) {
    case ValueKind::Constant:
        VERIFY(program->constants[value.id()].type == builtins::type_type);
        return (Type)value;
    case ValueKind::Parameter:
        VERIFY(parameterTypes[value.id()] == builtins::type_type);
        return (Type)value;
    case ValueKind::Program: {
        Program* valueProg = &context.programs[value.id()];
        VERIFY(!valueProg->isTemplate());
        VERIFY(valueProg->type() == builtins::type_type);
        return (Type)value;
    }
    default:
        VERIFY_NOT_REACHED();
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

    static glue::DeclarationNode* createScope(glue::Context& context, BuiltinId id) {
        context.pushStaticScope(glue::DeclarationNode::Kind::Type, nameOf(id), {});
        auto handle = context.newProgram();
        VERIFY(handle.id() == std::to_underlying(id));
        context.currentScope()->setProgram(handle);
        return context.currentScope();
    }

    BuiltinGenerator(glue::Context& context, BuiltinId id)
        : Generator(context, createScope(context, id)) {
        program->m_name = nameOf(id);
        buildParent(currentScope->declaringNode());
    }

    ~BuiltinGenerator() {
        buildSelf();
        program->setStatus(ProgramStatus::SignatureChecked);
        context.popScope();
    }
};

void Generator::generateBuiltins(glue::Context& context) {
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