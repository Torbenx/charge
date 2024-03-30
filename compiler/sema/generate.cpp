#include <sema/Generator.h>

namespace sema {

void Generator::emitExpr(Node node) {
    uint32_t nodeIndex = scratchBlock.size();
    scratchBlock.push_back(node);
    expressionStack.push_back({ nodeIndex });
}

void Generator::emitValueExpr(TaggedSourceLocation<NodeKind> location, Value value) {
    emitExpr({
        location,
        typeOfValue(value).toUint(),
        { .data2 = value.toUint() },
    });
}

void Generator::emitCompoundExpr(TaggedSourceLocation<NodeKind> location, Type type, int_t childCount) {
    int_t subTreeSize = 1;
    for (int_t i = 0; i < childCount; i++) {
        subTreeSize += scratchBlock[expressionStack.back().nodeIndex].subTreeSize();
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
    Value value = program->addExpression(expr);
    popExpression();
    return value;
}

Type Generator::makeProgramType(Program* targetProg) {
    if (targetProg->isTemplate()) {
        Value signature = program->addProgramLiteral(
            Program::Opcode::SignatureOf, builtins::template_signature_type, targetProg);

        emitValueExpr({ NodeKind::ConstantExpr, {} }, builtins::template_id_template);
        emitValueExpr({ NodeKind::ConstantExpr, {} }, signature);
        // generateParameterize(1);
        return verifyType(makeExpressionValue());
    } else {
        return verifyType(program->addProgramLiteral(Program::Opcode::TypeOf, builtins::type_type, targetProg));
    }
}

Value Generator::makeProgramLiteral(Program* targetProg) {
    Type type = makeProgramType(targetProg);
    return program->addProgramLiteral(Program::Opcode::ProgramLiteral, type, targetProg);
}

Value Generator::makeStaticAccess(Value base, Program* targetProg, ExternValue value) {
    ExternValue externType = targetProg->typeOf(value);
    Type type;
    if (externType.kind() == ValueKind::Builtin)
        type = verifyType((Value)externType);
    else
        type = verifyType(program->addStaticAccess(builtins::type_type, base, externType));
    return program->addStaticAccess(type, base, value);
}

std::optional<Value> Generator::lookupInScope(glue::DeclarationNode* scope, Word name) {
    using Kind = glue::DeclarationNode::Kind;
    if (scope->kind() == Kind::Namespace) {
        auto child = scope->findChild(name);
        if (child.has_value())
            return generateDeclarationLiteral(child);
        return std::nullopt;
    }
    VERIFY_NOT_REACHED();
}

Value Generator::generateDeclarationLiteral(glue::DeclarationNode* target) {
    using Kind = glue::DeclarationNode::Kind;
    switch (target->kind()) {
    case Kind::Namespace:
        return program->addLiteral(builtins::namespace_type, target);
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
    VERIFY_NOT_REACHED();
}

void Generator::implicitToType() {
    implicitCastTo(builtins::type_type);
}

void Generator::implicitCastTo(Type type) {
    auto expr = topExpression();
    if (expr.type() == type) {
        // nothing to do
        return;
    }
    VERIFY_NOT_REACHED();
}

Program* Generator::signatureCheck(glue::DeclarationNode* scope) {
    auto scopeProg = scope->program();
    if (scopeProg.has_value() && scopeProg->status() >= ProgramStatus::SignatureChecked)
        return scopeProg;

    if (!scopeProg.has_value())
        scope->setProgram(new Program());
    Generator generator(scope);
    VERIFY_NOT_REACHED();
}

Type Generator::typeOfValue(Value value) {
    switch (value.kind()) {
    case ValueKind::Local:
        return localValues[value.id()].type;
    case ValueKind::Constant:
        return program->constants[value.id()].type;
    case ValueKind::Builtin:
        return makeProgramType(&builtinPrograms[value.id()]);
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
    case ValueKind::Builtin: {
        Program* valueProg = &builtinPrograms[value.id()];
        VERIFY(!valueProg->isDependent());
        VERIFY(valueProg->type() == builtins::type_type);
        return (Type)value;
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

void Generator::buildDependentParents() {
    if (!program->hasDependentParent() || !dependentParents.empty())
        return;

    dependentParents.push_back((Value)program->dependentParent->parentParameter);
    Program* current = program;
    for (;;) {
        current = current->GetProgramLiteral(current->dependentParent->parent);
        if (!current->isDependent())
            return;
        ExternValue externParameter = current->dependentParent->parentParameter;
        dependentParents.push_back(makeStaticAccess(dependentParents.back(), current, externParameter));
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