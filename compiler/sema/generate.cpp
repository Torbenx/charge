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
    Expression expr = topExpression();
    Value value;
    if (expr.kind() == NodeKind::ConstantExpr)
        value = Value::fromUint(expr->u.data2);
    else
        value = program->addExpression(expr);
    popExpression();
    return value;
}

Type Generator::makeProgramType(Value programLiteral) {
    Program* targetProg = getProgramLiteral(programLiteral);
    if (targetProg->isTemplate()) {
        Value signature = program->addProgramLiteral(
            Program::Opcode::SignatureOf, builtins::template_signature_type, targetProg);

        auto [result, resultArgs] = program->addParameterize(builtins::type_type, builtins::template_id_template, 1);
        resultArgs.set(0, signature);
        return verifyType(result);
    } else {
        return verifyType(foldImpl(programLiteral, targetProg, {}, targetProg->type()));
    }
}

Value Generator::makeProgramLiteral(Program* targetProg) {
    auto untypedLiteral = program->addProgramLiteral(Program::Opcode::ProgramLiteral, builtins::error_type, targetProg);
    Type type = makeProgramType(untypedLiteral);
    return program->addProgramLiteral(Program::Opcode::ProgramLiteral, type, targetProg);
}

std::optional<Value> Generator::lookupInScope(glue::DeclarationNode* scope, Word name) {
    using Kind = glue::DeclarationNode::Kind;
    if (scope->kind() == Kind::Namespace) {
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
    glue::DeclarationNode* lookupScope = currentScope;
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
}

void Generator::implicitToType() {
    implicitCastTo(builtins::type_type);
}

void Generator::implicitCastTo(Type type) {
    if (topExpression().type() != type)
        emitCompoundExpr({ NodeKind::ImplicitConversion, {} }, type, 1);
}

Program* Generator::signatureCheck(glue::DeclarationNode* scope) {
    auto scopeProg = scope->program();
    if (scopeProg.has_value() && scopeProg->status() >= ProgramStatus::SignatureChecked)
        return scopeProg;

    if (!scopeProg.has_value())
        scope->setProgram(new Program());
    else
        VERIFY(scopeProg->status() == ProgramStatus::Unchecked);
    Generator generator(scope);
    generator.visitDeclaration();
    return scope->program().value();
}

Value Generator::fold(Value base, ExternValue v) {
    if (v.kind() == ValueKind::Builtin)
        return (Value)v;
    VERIFY(v.kind() == ValueKind::Constant);

    if (base.kind() == ValueKind::Builtin)
        return foldImpl(base, &builtinPrograms[base.id()], {}, v);
    VERIFY(base.kind() == ValueKind::Constant);

    const auto& baseConst = program->constants[base.id()];
    if (baseConst.op == Program::Opcode::ProgramLiteral) {
        Program* baseProg = baseConst.u.program;
        VERIFY(!baseProg->isDependent());
        return foldImpl(base, baseProg, {}, v);
    } else if (baseConst.op == Program::Opcode::Parameterize) {
        Program* baseProg = getProgramLiteral(baseConst.u.parameterize.base);
        return foldImpl(base, baseProg, parameterizeArguments(base), v);
    }
    VERIFY_NOT_REACHED();
}

Value Generator::foldImpl(Value base, Program* baseProg, std::span<const Value> arguments, ExternValue v) {
    if (v.kind() == ValueKind::Builtin)
        return (Value)v;
    VERIFY(v.kind() == ValueKind::Constant);

    const auto& vConst = baseProg->constants[v.id()];

    auto type = [&]() -> Type {
        return verifyType(foldImpl(base, baseProg, arguments, vConst.type));
    };

    switch (vConst.op) {
    case Program::Opcode::NamespaceLiteral:
        return program->addNamespaceLiteral(vConst.u.declarationNode);
    case Program::Opcode::ProgramLiteral:
        return makeProgramLiteral(vConst.u.program);
    case Program::Opcode::SignatureOf:
        return program->addProgramLiteral(Program::Opcode::SignatureOf, type(), vConst.u.program);
    case Program::Opcode::RemoteExpression: {
        Value exprBase = foldImpl(base, baseProg, arguments, vConst.u.remoteExpression.base);
        return program->addRemoteExpression(type(), exprBase, vConst.u.remoteExpression.expressionIndex);
    }
    case Program::Opcode::Expression:
        return program->addRemoteExpression(type(), base, vConst.u.expressionIndex);
    case Program::Opcode::Parameter:
        return arguments[vConst.u.parameterIndex];
    case Program::Opcode::Parameterize: {
        Value resultBase = foldImpl(base, baseProg, arguments, vConst.u.parameterize.base);
        auto externArgs = parameterizeArguments(baseProg, v);
        auto [result, resultArgs] = program->addParameterize(type(), resultBase, externArgs.size());
        for (int_t argIndex = 0; argIndex < (int_t)externArgs.size(); argIndex++) {
            resultArgs.set(argIndex, foldImpl(base, baseProg, arguments, externArgs[argIndex]));
        }
        return result;
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Program* Generator::getProgramLiteral(Value value) {
    if (value.kind() == ValueKind::Builtin)
        return &builtinPrograms[value.id()];
    VERIFY(value.kind() == ValueKind::Constant);
    const auto& c = program->constants[value.id()];
    VERIFY(c.op == Program::Opcode::ProgramLiteral);
    return c.u.program;
}

std::span<const Value> Generator::parameterizeArguments(Value value) {
    VERIFY(value.kind() == ValueKind::Constant);
    const auto& param = program->constants[value.id()].u.parameterize;
    return std::span<const Value>(&program->parameterizeArguments[param.firstArgumentIndex], param.argumentCount);
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
    case ValueKind::Constant: {
        return program->constants[value.id()].type;
    }
    case ValueKind::Builtin:
        return makeProgramType(value);
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