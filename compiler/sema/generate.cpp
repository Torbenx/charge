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

Value Generator::makeProgramLiteral(Program* targetProg) {
    if (targetProg->isTemplate()) {
        Value signature = program->addProgramLiteral(
            Program::Opcode::SignatureOf, builtins::template_signature_type, targetProg);

        auto [type, tempalteIdArgs] = program->addParameterize(builtins::type_type, builtins::template_id_template, 1);
        tempalteIdArgs.set(0, signature);
        return program->addProgramLiteral(Program::Opcode::ProgramLiteral, verifyType(type), targetProg);
    }

    Value result = program->addProgramLiteral(Program::Opcode::ProgramLiteral, Type(), targetProg);
    if (targetProg->isDependent())
        result = program->addParameterize(Type(), result, 0, targetProg->inheritedParameterCount);
    program->constants[result.id()].type = verifyType(fold(result, targetProg->type()));
    return result;
}

void Generator::inheriteParameters(Program* parentProg) {
    int_t parameterCount = parentProg->parameters.size();
    VERIFY(parameterCount > 0);
    Value baseValue = program->addProgramLiteral(Program::Opcode::ProgramLiteral, Type(), parentProg);
    auto [parentValue, parentArgs] = program->addParameterize(Type(), baseValue, parameterCount);
    BaseProgram base = asProgram(parentValue);
    VERIFY(parentArgs.firstIndex == 0);
    for (int_t i = 0; i < parameterCount; i++) {
        ExternValue parentParameter = parentProg->parameterValue(i);
        {
            VERIFY(parentParameter.kind() == ValueKind::Constant);
            const auto& parentParameterConst = parentProg->constants[parentParameter.id()];
            VERIFY(parentParameterConst.op == Program::Opcode::Parameter);
            VERIFY(parentParameterConst.u.parameterIndex == i);
        }
        Type type = verifyType(fold(base, base->typeOf(parentParameter)));
        parentArgs.set(i, program->addInheritedParameter(type, std::nullopt));
    }
    program->constants[parentValue.id()].type = verifyType(fold(base, base->type()));
}

std::optional<Value> Generator::lookupInScope(glue::DeclarationNode* scope, Word name) {
    using Kind = glue::DeclarationNode::Kind;
    if (scope->kind() == Kind::Namespace || scope->kind() == Kind::Type) {
        if (scope->kind() == Kind::Type) {
            VERIFY(scope->program().has_value());
            VERIFY(scope->program()->status() >= ProgramStatus::SignatureChecked);
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
    case Kind::Function:
    case Kind::Variable: {
        Program* targetProg = signatureCheck(target);
        return makeProgramLiteral(targetProg);
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
    if (name == parse::words["type"]) {
        emitValueExpr({ NodeKind::ConstantExpr, tok->location() }, builtins::type_type);
        return;
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateParameterizeExpr(int_t argumentCount) {
    Expression baseExpr = topExpression(argumentCount);
    if (baseExpr.kind() == NodeKind::ConstantExpr) {
        Value baseValue = Value::fromUint(baseExpr->u.data2);
        auto maybeLiteral = getProgramLiteral(baseValue);
        if (maybeLiteral.has_value()) {
            Program* baseProg = maybeLiteral.value();
            VERIFY(baseProg->isTemplate());
            int_t parameterCount = baseProg->parameters.size();
            auto [result, resultArgs] = program->addParameterize(Type(), baseValue, parameterCount);
            DeductionState state(parameterCount);
            for (int_t i = 0; i < (int_t)baseProg->inheritedParameterCount; i++) {
                state.explicitArgumentsMap[i] = true;
                resultArgs.set(i, program->parameterValue(i));
            }
            auto base = asProgram(result);

            int_t pIndex = baseProg->inheritedParameterCount + baseProg->implicitParameterCount;
            int_t aIndex = 0;
            for (; aIndex < argumentCount; aIndex++, pIndex++) {
                ExternValue pValue = baseProg->parameterValue(pIndex);
                ExternValue pType = baseProg->typeOf(pValue);
                Expression argument = topExpression(argumentCount - 1 - aIndex);
                implicitCastTo(state, pType, base, argument);
                if (argument.kind() == NodeKind::ConstantExpr) {
                    state.explicitArgumentsMap[pIndex] = true;
                    base.arguments[pIndex] = Value::fromUint(argument->u.data2);
                } else {
                    VERIFY_NOT_REACHED();
                }
            }
            for (int_t i = 0; i < parameterCount; i++)
                VERIFY(base.arguments[i] != INVALID_VALUE);
            popExpressions(argumentCount + 1);
            Type type = verifyType(fold(base, base->type()));
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

void Generator::implicitCastTo(DeductionState& state, ExternValue pType, BaseProgram pBase, Expression arg) {
    bool sameType = staticMatch(state, pType, pBase, arg.type());
    VERIFY(sameType);
}

BaseProgram Generator::asProgram(Value base) {
    if (base.kind() == ValueKind::Builtin) {
        Program* baseProg = &builtinPrograms[base.id()];
        VERIFY(!baseProg->isDependent());
        return { baseProg, base, {} };
    }
    VERIFY(base.kind() == ValueKind::Constant);

    const auto& baseConst = program->constants[base.id()];
    if (baseConst.op == Program::Opcode::ProgramLiteral) {
        Program* baseProg = baseConst.u.program;
        VERIFY(!baseProg->isDependent());
        return { baseProg, base, {} };
    } else if (baseConst.op == Program::Opcode::Parameterize) {
        Program* baseProg = getProgramLiteral(baseConst.u.parameterize.base);
        return { baseProg, base, parameterizeArguments(base) };
    }
    VERIFY_NOT_REACHED();
}

// pValue and aValue must be known to have the same type
bool Generator::staticMatch(DeductionState& state, ExternValue pValue, BaseProgram pBase, Value aValue) {
    if (pValue.kind() == ValueKind::Constant) {
        const auto& pConst = pBase->constants[pValue.id()];
        if (pConst.op == Program::Opcode::Parameter) {
            int_t index = pConst.u.parameterIndex;
            if (state.isExplicitArgument(index))
                return pBase.arguments[index] == aValue; // TODO: better comparison

            if (pBase.arguments[index] == INVALID_VALUE) {
                pBase.arguments[index] = aValue;
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
    if (pValue.kind() == ValueKind::Builtin)
        return pValue == aValue;
    VERIFY(pValue.kind() == ValueKind::Constant);
    const auto& pConst = pBase->constants[pValue.id()];
    const auto& aConst = program->constants[aValue.id()];

    if (pConst.op != aConst.op)
        return false;
    switch (pConst.op) {
    case Program::Opcode::NamespaceLiteral:
        return pConst.u.declarationNode == aConst.u.declarationNode;
    case Program::Opcode::ProgramLiteral:
        return pConst.u.program == aConst.u.program;
    case Program::Opcode::SignatureOf:
        return pConst.u.program == aConst.u.program; // TODO: different programs can have the same signature
    case Program::Opcode::Parameterize: {
        const auto& pPara = pConst.u.parameterize;
        const auto& aPara = aConst.u.parameterize;
        if (!staticMatch(state, pPara.base, pBase, aPara.base))
            return false;
        VERIFY(pPara.argumentCount == aPara.argumentCount);
        for (int_t i = 0; i < (int_t)pPara.argumentCount; i++) {
            ExternValue pArgument = pBase->parameterizeArguments[pPara.firstArgumentIndex + i];
            Value aArgument = program->parameterizeArguments[aPara.firstArgumentIndex + i];
            if (!staticMatch(state, pArgument, pBase, aArgument))
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
    if (v.kind() == ValueKind::Builtin)
        return (Value)v;
    VERIFY(v.kind() == ValueKind::Constant);

    const auto& vConst = base->constants[v.id()];

    auto type = [&]() -> Type {
        return verifyType(fold(base, vConst.type));
    };

    switch (vConst.op) {
    case Program::Opcode::NamespaceLiteral:
        return program->addNamespaceLiteral(vConst.u.declarationNode);
    case Program::Opcode::ProgramLiteral:
        return makeProgramLiteral(vConst.u.program);
    case Program::Opcode::SignatureOf:
        return program->addProgramLiteral(Program::Opcode::SignatureOf, type(), vConst.u.program);
    case Program::Opcode::RemoteExpression: {
        Value exprBase = fold(base, vConst.u.remoteExpression.base);
        return program->addRemoteExpression(type(), exprBase, vConst.u.remoteExpression.expressionIndex);
    }
    case Program::Opcode::Expression:
        return program->addRemoteExpression(type(), base.value, vConst.u.expressionIndex);
    case Program::Opcode::Parameter:
        return base.arguments[vConst.u.parameterIndex];
    case Program::Opcode::Parameterize: {
        Value resultBase = fold(base, vConst.u.parameterize.base);
        auto externArgs = parameterizeArguments(base.program, v);
        auto [result, resultArgs] = program->addParameterize(type(), resultBase, externArgs.size());
        for (int_t argIndex = 0; argIndex < (int_t)externArgs.size(); argIndex++) {
            resultArgs.set(argIndex, fold(base, externArgs[argIndex]));
        }
        return result;
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Program* Generator::signatureCheck(glue::DeclarationNode* scope) {
    auto scopeProg = scope->program();
    if (scopeProg.has_value() && scopeProg->status() >= ProgramStatus::SignatureChecked)
        return scopeProg;

    if (!scopeProg.has_value())
        scope->setProgram(new Program());
    else
        VERIFY(scopeProg->status() == ProgramStatus::Unchecked);

    Generator g(scope);
    auto* parent = scope->declaringNode();
    if (parent->program().has_value() && parent->program()->isDependent()) {
        g.inheriteParameters(parent->program().value());
    }

    g.visitDeclaration();
    return scope->program().value();
}

std::optional<Program*> Generator::getProgramLiteral(Value value) {
    if (value.kind() == ValueKind::Builtin)
        return &builtinPrograms[value.id()];
    const auto& c = program->constants[value.id()];
    if (c.op == Program::Opcode::ProgramLiteral)
        return c.u.program;
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
    case ValueKind::Builtin: {
        Program* valueProg = &builtinPrograms[value.id()];
        if (!valueProg->isDependent())
            return verifyType(fold(BaseProgram { valueProg, value, {} }, valueProg->type()));
        return typeOf(makeProgramLiteral(valueProg));
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
    case ValueKind::Constant: {
        const auto& c = program->constants[value.id()];
        if (c.op == Program::Opcode::ProgramLiteral) {
            Program* valueProg = c.u.program;
            VERIFY(!valueProg->isTemplate());
            VERIFY(valueProg->type() == builtins::type_type);
        } else {
            VERIFY(program->constants[value.id()].type == builtins::type_type);
        }
        return (Type)value;
    }
    case ValueKind::Builtin: {
        Program* valueProg = &builtinPrograms[value.id()];
        VERIFY(!valueProg->isTemplate());
        VERIFY(valueProg->type() == builtins::type_type);
        return (Type)value;
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

std::array<Program, std::to_underlying(BuiltinId::COUNT)> builtinPrograms = [] {
    std::array<Program, std::to_underlying(BuiltinId::COUNT)> programs;
    auto prog = [&programs](BuiltinId id) { return &programs[std::to_underlying(id)]; };

    {
        Generator g { prog(BuiltinId::type_type) };
        g.program->setType(builtins::type_type);
    }
    {
        Generator g { prog(BuiltinId::namespace_type) };
        g.program->setType(builtins::type_type);
    }
    {
        Generator g { prog(BuiltinId::function_signature_type) };
        g.program->setType(builtins::type_type);
    }
    {
        Generator g { prog(BuiltinId::template_signature_type) };
        g.program->setType(builtins::type_type);
    }

    // typeof(tempalte(T: type) => expr) = template_id{template(T: type) -> typeof(expr)}
    // cast{type}(template(T: type) -> type_expr) = template_id{template(T: type) -> type_expr}

    // template(sig: template_signature) struct template_id: { }
    // typof(template_id) = typeof(template(sig: template_signature) => template_id{sig})
    //                    = template_id{template(sig: template_signature) -> typeof(template_id{sig})}
    //                    = template_id{template(sig: template_signature) -> type}
    {
        Generator g { prog(BuiltinId::function_id_template) };
        g.program->addExplicitParameter(Word(), builtins::template_signature_type, {});
        g.program->setType(builtins::type_type);
    }

    // template(sig: function_signature) struct function_id: { }
    // typeof(function_id) = typeof(template(sig: function_signature) => function_id{sig})
    //                     = template_id{template(sig: function_signature) -> typeof(function_id{sig})}
    //                     = template_id{template(sig: function_signature) -> type}
    {
        Generator g { prog(BuiltinId::function_id_template) };
        g.program->addExplicitParameter(Word(), builtins::function_signature_type, {});
        g.program->setType(builtins::type_type);
    }

    // typeof( (arg = expr) ) = cast{type}( (arg = typeof(expr)) ) = tuple{cast{tuple_signature}( (arg = typeof(expr)) )}

    return programs;
}();

}