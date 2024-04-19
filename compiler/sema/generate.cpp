#include <glue/Context.h>
#include <sema/Generator.h>

namespace sema {

void Generator::emitExpr(Node node) {
    uint32_t nodeIndex = expressionScratch.size();
    expressionScratch.push_back(node);
    expressionStack.push_back({ nodeIndex });
}

void Generator::emitValueExpr(TaggedSourceLocation<NodeKind> location, Value value) {
    emitExpr({
        location,
        typeOf(value).toUint(),
        { .data2 = value.toUint() },
    });
}

void Generator::emitCompoundExpr(TaggedSourceLocation<NodeKind> location, Type type, int_t childCount) {
    int_t subTreeSize = 1;
    for (int_t i = 0; i < childCount; i++) {
        subTreeSize += expressionScratch[expressionStack.back().nodeIndex].subTreeSize();
        expressionStack.pop_back();
    }
    emitExpr({
        location,
        type.toUint(),
        { .compound = { .childrenCount = (uint16_t)childCount, .subTreeSize = (uint16_t)subTreeSize } },
    });
}

Value Generator::makeExpressionValue() {
    Value value = makeExpressionValue(topExpression());
    popExpression();
    return value;
}

Value Generator::makeExpressionValue(Expression expr) {
    if (expr.kind() == NodeKind::ConstantExpr)
        return Value::fromUint(expr->u.data2);
    else
        return program->addExpression(expr);
}

Type Generator::makeTemplateIdFor(ProgramHandle targetHandle) {
    Program* targetProg = &context.programs[targetHandle.id()];
    VERIFY(targetProg->isTemplate());
    std::array arguments { program->addSignatureOf(builtins::template_signature_type, targetHandle) };
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
        program->m_parent = program->addNamespaceLiteral(parentNode);
        return;
    }

    ProgramHandle parentHandle = signatureCheck(context, parentNode);
    Program* parentProg = &context.programs[parentHandle.id()];
    if (!parentProg->isDependent()) {
        program->m_parent = makeProgramValue(parentHandle);
        return;
    }

    // inherite parameters
    int_t parameterCount = parentProg->parameters.size();
    VERIFY(parameterCount > 0);
    std::vector<Value> arguments(parameterCount, INVALID_VALUE);
    for (int_t i = 0; i < parameterCount; i++)
        arguments[i] = program->addInheritedParameter(Type(), std::nullopt);

    Value parentValue = program->addParameterize(Type(), parentHandle, arguments);
    BaseProgram base = asProgram(parentValue);
    for (int_t i = 0; i < parameterCount; i++) {
        ExternValue parentParameter = parentProg->parameterValue(i);
        {
            VERIFY(parentParameter.kind() == ValueKind::Constant);
            const auto& parentParameterConst = parentProg->constants[parentParameter.id()];
            VERIFY(parentParameterConst.op == Program::Opcode::Parameter);
            VERIFY(parentParameterConst.u.parameterIndex == i);
        }
        Type type = verifyType(fold(base, base->typeOf(parentParameter)));
        program->constants[base.arguments[i].id()].type = type;
    }
    program->constants[parentValue.id()].type = verifyType(fold(base, base->type()));
    program->m_parent = parentValue;
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
    for (const auto& entry : localDeclarations) {
        if (name == entry.name) {
            emitValueExpr({ NodeKind::ReferenceExpr, tok->location() }, entry.value);
            return;
        }
    }
    auto result = lookupCache.get(name);
    if (result.has_value()) {
        emitValueExpr({ NodeKind::ConstantExpr, tok->location() }, result.value());
        return;
    }
    glue::DeclarationNode* lookupScope = currentScope->declaringNode();
    while (lookupScope != nullptr) {
        auto lookup = lookupInScope(lookupScope, name);
        if (lookup.has_value()) {
            lookupCache.insert(name, lookup.value());
            emitValueExpr({ NodeKind::ConstantExpr, tok->location() }, lookup.value());
            return;
        }
        lookupScope = lookupScope->declaringNode();
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateParameterizeExpr(int_t argumentCount) {
    Expression baseExpr = topExpression(argumentCount);
    if (baseExpr.kind() == NodeKind::ConstantExpr) {
        Value baseValue = Value::fromUint(baseExpr->u.data2);
        if (baseValue.kind() == ValueKind::Program) {
            Program* baseProg = &context.programs[baseValue.id()];
            VERIFY(baseProg->isTemplate());
            int_t parameterCount = baseProg->parameters.size();
            std::vector<Value> arguments(parameterCount, INVALID_VALUE);
            DeductionState state(parameterCount);
            for (int_t i = 0; i < (int_t)baseProg->inheritedParameterCount; i++) {
                state.explicitArgumentsMap[i] = true;
                arguments[i] = program->parameterValue(i);
            }

            int_t pIndex = baseProg->inheritedParameterCount + baseProg->implicitParameterCount;
            int_t aIndex = 0;
            for (; aIndex < argumentCount; aIndex++, pIndex++) {
                ExternValue pValue = baseProg->parameterValue(pIndex);
                ExternValue pType = baseProg->typeOf(pValue);
                Expression argument = topExpression(argumentCount - 1 - aIndex);
                implicitCastTo(state, pType, baseProg, arguments, argument);
                if (argument.kind() == NodeKind::ConstantExpr) {
                    state.explicitArgumentsMap[pIndex] = true;
                    arguments[pIndex] = Value::fromUint(argument->u.data2);
                } else {
                    VERIFY_NOT_REACHED();
                }
            }
            for (int_t i = 0; i < parameterCount; i++)
                VERIFY(arguments[i] != INVALID_VALUE);
            popExpressions(argumentCount + 1);
            Value result = program->addParameterize(Type(), baseValue.program(), arguments);
            Type type = verifyType(fold(result, baseProg->type()));
            program->constants[result.id()].type = type;
            emitValueExpr({ NodeKind::ConstantExpr, {} }, result);
            return;
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::implicitToType() {
    implicitCastTo(builtins::type_type);
}

void Generator::implicitCastTo(Type type) {
    if (topExpression().type() != type)
        emitCompoundExpr({ NodeKind::ImplicitConversion, {} }, type, 1);
}

void Generator::implicitCastTo(DeductionState& state, ExternValue pType, Program* pBase, std::span<Value> arguments, Expression arg) {
    bool sameType = staticMatch(state, pType, pBase, arguments, arg.type());
    VERIFY(sameType);
}

BaseProgram Generator::asProgram(Value base) {
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

// pValue and aValue must be known to have the same type
bool Generator::staticMatch(DeductionState& state, ExternValue pValue, Program* pBase, std::span<Value> arguments, Value aValue) {
    if (pValue.kind() == ValueKind::Constant) {
        const auto& pConst = pBase->constants[pValue.id()];
        if (pConst.op == Program::Opcode::Parameter) {
            int_t index = pConst.u.parameterIndex;
            if (state.isExplicitArgument(index))
                return arguments[index] == aValue; // TODO: better comparison

            if (arguments[index] == INVALID_VALUE) {
                arguments[index] = aValue;
                return true;
            }
            return false; // TODO: compare with existing deduction
        }
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
        return pBase->programTranslationBuffer[pValue.id()] == aValue.program();
    VERIFY(pValue.kind() == ValueKind::Constant);
    const auto& pConst = pBase->constants[pValue.id()];
    const auto& aConst = program->constants[aValue.id()];

    if (pConst.op != aConst.op)
        return false;
    switch (pConst.op) {
    case Program::Opcode::NamespaceLiteral:
        return pConst.u.declarationNode == aConst.u.declarationNode;
    case Program::Opcode::SignatureOf:
        return pConst.u.signatureProgram == aConst.u.signatureProgram; // TODO: different programs can have the same signature
    case Program::Opcode::Parameterize: {
        const auto& pPara = pConst.u.parameterize;
        const auto& aPara = aConst.u.parameterize;
        if (pPara.base != aPara.base)
            return false;
        VERIFY(pPara.argumentCount == aPara.argumentCount);
        for (int_t i = 0; i < (int_t)pPara.argumentCount; i++) {
            ExternValue pArgument = pBase->parameterizeArguments[pPara.firstArgumentIndex + i];
            Value aArgument = program->parameterizeArguments[aPara.firstArgumentIndex + i];
            if (!staticMatch(state, pArgument, pBase, arguments, aArgument))
                return false;
        }
        return true;
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Value Generator::fold(Value base, ExternValue v) {
    return fold(asProgram(base), v);
}

Value Generator::fold(BaseProgram base, ExternValue v) {
    if (v.kind() == ValueKind::Program)
        return makeProgramValue(base->programTranslationBuffer[v.id()]);
    VERIFY(v.kind() == ValueKind::Constant);

    const auto& vConst = base->constants[v.id()];

    auto type = [&]() -> Type {
        return verifyType(fold(base, vConst.type));
    };

    switch (vConst.op) {
    case Program::Opcode::NamespaceLiteral:
        return program->addNamespaceLiteral(vConst.u.declarationNode);
    case Program::Opcode::SignatureOf:
        return program->addSignatureOf(type(), vConst.u.signatureProgram);
    case Program::Opcode::RemoteExpression: {
        Value exprBase = fold(base, vConst.u.remoteExpression.base);
        return program->addRemoteExpression(type(), exprBase, vConst.u.remoteExpression.expressionIndex);
    }
    case Program::Opcode::Expression:
        return program->addRemoteExpression(type(), base.value, vConst.u.expressionIndex);
    case Program::Opcode::Parameter:
        return base.arguments[vConst.u.parameterIndex];
    case Program::Opcode::Parameterize: {
        auto externArgs = parameterizeArguments(base.program, v);
        std::vector<Value> foldedArgs(externArgs.size(), INVALID_VALUE);
        for (int_t argIndex = 0; argIndex < (int_t)externArgs.size(); argIndex++)
            foldedArgs[argIndex] = fold(base, externArgs[argIndex]);
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
    g.buildParent(scope->declaringNode());

    g.visitDeclaration();
    return scope->program().value();
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
    case ValueKind::Local:
        return localValues[value.id()].type;
    case ValueKind::Constant:
        return program->constants[value.id()].type;
    case ValueKind::Program: {
        Program* valueProg = &context.programs[value.id()];
        if (!valueProg->isDependent())
            return verifyType(fold(BaseProgram { valueProg, value, {} }, valueProg->type()));
        return makeTemplateIdFor(value.program());
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Type Generator::verifyType(Value value) {
    switch (value.kind()) {
    case ValueKind::Local:
        VERIFY(localValues[value.id()].type == builtins::type_type);
        return (Type)value;
    case ValueKind::Constant:
        VERIFY(program->constants[value.id()].type == builtins::type_type);
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
        program->setStatus(ProgramStatus::SignatureChecked);
        context.popScope();
    }
};

void Generator::generateBuiltins(glue::Context& context) {
    {
        BuiltinGenerator g { context, BuiltinId::error_type };
        g.program->setType(builtins::type_type);
    }
    {
        BuiltinGenerator g { context, BuiltinId::type_type };
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
        g.program->addExplicitParameter(Word(), builtins::template_signature_type, {});
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
        g.program->addExplicitParameter(Word(), builtins::function_signature_type, {});
        g.program->setType(builtins::type_type);
    }

    // typeof( (arg = expr) ) = cast{type}( (arg = typeof(expr)) ) = tuple{cast{tuple_signature}( (arg = typeof(expr)) )}
}

}