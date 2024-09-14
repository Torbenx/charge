#include <sema/Context.h>
#include <sema/Generator.h>

#include <ranges>

namespace sema {

Instruction& Generator::topInstruction(int_t n) {
    auto entry = *(expressionStack.end() - n - 1);
    return instructionScratch[entry.endOffset - 1];
}

Expression Generator::topExpression(int_t n) {
    auto entry = *(expressionStack.end() - n - 1);
    auto prevEntry = *(expressionStack.end() - n - 2);
    return Expression(&instructionScratch[entry.endOffset - 1], entry.endOffset - prevEntry.endOffset);
}

void Generator::popExpression() {
    popExpressions(1);
}

void Generator::popExpressions(int_t n) {
    VERIFY(n < (int_t)expressionStack.size());
    int_t newSize = (expressionStack.end() - n - 1)->endOffset;
    instructionScratch.erase(instructionScratch.begin() + newSize, instructionScratch.end());
    expressionStack.erase(expressionStack.end() - n, expressionStack.end());
}

void Generator::emitControl(Opcode op, SourceLocation location, int_t childCount, InstructionData data) {
    VERIFY(expressionStack.back().endOffset == instructionScratch.size());
    expressionStack.resize(expressionStack.size() - childCount);
    instructionScratch.emplace_back(op, location, data);
    expressionStack.back().endOffset = (uint32_t)instructionScratch.size();
}

void Generator::emitExpression(Opcode op, SourceLocation location, int_t childCount, Type type, ExpressionData data) {
    VERIFY(expressionStack.back().endOffset == instructionScratch.size());
    expressionStack.resize(expressionStack.size() - childCount);
    instructionScratch.emplace_back(op, location, InstructionData { .expr { type, data } });
    expressionStack.push_back({ .endOffset = (uint32_t)instructionScratch.size() });
}

void Generator::emitConstantExpr(SourceLocation location, Value value) {
    emitExpression(Opcode::Constant, location, 0, typeOf(value), { .constant = value });
}

void Generator::emitReferenceExpr(SourceLocation location, int_t localValueIndex) {
    return emitExpression(
        Opcode::Reference, location, 0, localValues[localValueIndex].type,
        { .referencedLocalIndex = (uint32_t)localValueIndex });
}

Value Generator::makeExpressionValue() {
    Value value = makeExpressionValue(topExpression());
    popExpression();
    return value;
}

Value Generator::makeExpressionValue(Expression expr) {
    if (expr.opcode() == Opcode::Constant)
        return expr.data().constant;
    else
        return program->addExpression(expr);
}

Value Generator::makeTemplateSignature(Value templateProg) {
    if (templateProg.kind() == ValueKind::Program)
        return Value(ValueKind::TemplateSignature$Program, templateProg.id());
    if (templateProg.kind() == ValueKind::Parameterize)
        return Value(ValueKind::TemplateSignature$Parameterize, templateProg.id());
    VERIFY_NOT_REACHED();
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

Type Generator::makeTemplateIdFor(Value templateProg) {
    std::array arguments { makeTemplateSignature(templateProg) };
    return verifyType(program->addParameterize(builtins::template_id_template.program(), arguments));
}

Value Generator::makeParameterize(ProgramHandle base, std::span<const Value> arguments) {
    Program* baseProg = context.program(base);
    VERIFY(arguments.size() == baseProg->inheritedParameterCount || arguments.size() == baseProg->parameters.size());
    if (arguments.empty())
        return Value(base);
    return program->addParameterize(base, arguments);
}

Value Generator::inheriteParameters(ScopeValue parent) {
    if (parent.kind() == ValueKind::Namespace)
        return (Value)parent.nsHandle();

    VERIFY(parent.kind() == ValueKind::Program);

    ProgramHandle parentHandle = parent.program();
    signatureCheck(context, parentHandle);
    Program* parentProg = context.program(parentHandle);
    if (!parentProg->isDependent())
        return (Value)parentHandle;

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

Value Generator::generateDeclarationLiteral(ScopeValue rawValue, std::span<const Value> baseArgs) {
    auto makeProgramValue = [this, baseArgs](ProgramHandle targetHandle) {
        Program* targetProg = context.program(targetHandle);
        VERIFY(targetProg->inheritedParameterCount <= baseArgs.size());
        if (targetProg->inheritedParameterCount > 0)
            return program->addParameterize(targetHandle, baseArgs);
        return Value(targetHandle);
    };

    if (rawValue.kind() == ValueKind::Namespace)
        return (Value)rawValue.nsHandle();
    VERIFY(rawValue.kind() == ValueKind::Program);

    ProgramHandle progHandle = rawValue.program();
    Program* prog = context.program(progHandle);
    signatureCheck(context, progHandle);
    switch (prog->kind()) {
    case ProgramKind::Object:
    case ProgramKind::Type:
    case ProgramKind::Function:
        return makeProgramValue(progHandle);
    case ProgramKind::Value: {
        Value progValue = makeProgramValue(progHandle);
        if (prog->isTemplate())
            return progValue;
        return fold(progValue, cast<ValueProgram>(prog)->value());
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

void Generator::generateIdentifierExpr() {
    Word name = Word::fromUint(tok->data());
    if (name == parse::words["false"]) {
        emitConstantExpr(tok->location(), Value(ValueKind::BooleanLiteral, 0));
        return;
    }
    if (name == parse::words["true"]) {
        emitConstantExpr(tok->location(), Value(ValueKind::BooleanLiteral, 1));
        return;
    }
    for (auto lookupCtx : std::views::reverse(lookupStack)) {
        switch (lookupCtx.kind()) {
        case LookupContext::Kind::Namespace: {
            auto result = lookupCtx.getNamespace()->getDeclaration(name);
            if (result.has_value()) {
                emitConstantExpr(tok->location(), generateDeclarationLiteral(result.value(), {}));
                return;
            }
            continue;
        }
        case LookupContext::Kind::Type: {
            TypeProgram* prog = lookupCtx.getType();
            auto result = lookupInType(prog, identityParameterMap(prog), name);
            if (result.has_value()) {
                emitConstantExpr(tok->location(), result.value());
                return;
            }
            for (int_t i = prog->inheritedParameterCount; i < (int_t)prog->parameters.size(); i++) {
                if (prog->parameters[i].name == name) {
                    emitConstantExpr(tok->location(), Value(ValueKind::Parameter, i));
                    return;
                }
            }
            continue;
        };
        case LookupContext::Kind::TemplateParameters: {
            Program* prog = lookupCtx.getTemplateParameters();
            for (int_t i = prog->inheritedParameterCount; i < (int_t)prog->parameters.size(); i++) {
                if (prog->parameters[i].name == name) {
                    emitConstantExpr(tok->location(), Value(ValueKind::Parameter, i));
                    return;
                }
            }
            continue;
        }
        case LookupContext::Kind::Local: {
            Generator& g = *lookupCtx.getLocal();
            for (auto entry : g.localLookupEntries) {
                if (entry.name == name) {
                    VERIFY(&g == this);
                    if (entry.isLocalValue())
                        emitReferenceExpr(tok->location(), entry.localValueIndex());
                    else
                        emitConstantExpr(tok->location(), entry.constant());
                    return;
                }
            }
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateParameterizeExpr(int_t argumentCount) {
    Expression baseExpr = topExpression(argumentCount);
    if (baseExpr.opcode() == Opcode::Constant) {
        Value baseValue = baseExpr.data().constant;

        auto generate = [this, argumentCount](DeductionState state) {
            int_t parameterCount = state.arguments.size();
            int_t pIndex = state.program->inheritedParameterCount;
            int_t aIndex = 0;
            for (; aIndex < argumentCount; aIndex++, pIndex++) {
                // Find next explicit parameter
                while (pIndex < parameterCount && state.program->parameters[pIndex].implicit())
                    pIndex += 1;
                VERIFY(pIndex < parameterCount);

                ExternValue pType = state.program->parameters[pIndex].type;
                Expression argument = topExpression(argumentCount - 1 - aIndex);
                implicitCastTo(state, pType, argument);
                state.explicitArgument(pIndex, makeExpressionValue(argument));
            }
            VERIFY(pIndex == parameterCount);
            VERIFY(state.isComplete());
            popExpressions(argumentCount + 1);
            Value result = makeParameterize(state.programHandle, state.arguments);
            if (state.program->kind() == ProgramKind::Value)
                result = fold(result, cast<ValueProgram>(state.program)->value());

            emitConstantExpr({}, result);
        };

        if (baseValue.kind() == ValueKind::Program) {
            Program* baseProg = context.program(baseValue.program());
            VERIFY(baseProg->isTemplate());
            VERIFY(baseProg->inheritedParameterCount == 0);
            DeductionState state(baseProg, baseValue.program(), baseProg->parameters.size());
            generate(std::move(state));
            return;
        }
        if (baseValue.kind() == ValueKind::Parameterize) {
            auto basePara = program->getParameterize(baseValue);
            Program* baseProg = context.program(basePara.base);
            VERIFY(baseProg->isTemplate());
            VERIFY(baseProg->inheritedParameterCount == basePara.arguments.size());
            DeductionState state(baseProg, basePara.base, baseProg->parameters.size());
            for (int_t i = 0; i < (int_t)baseProg->inheritedParameterCount; i++)
                state.explicitArgument(i, basePara.arguments[i]);
            generate(std::move(state));
            return;
        }
    }
    VERIFY_NOT_REACHED();
}

Generator::CallTarget Generator::resolveCallTarget() {
    auto baseExpr = topExpression();
    // TODO: Allow only function and type programs
    if (baseExpr.opcode() == Opcode::Constant) {
        auto baseValue = baseExpr.data().constant;
        if (baseValue.kind() == ValueKind::Program) {
            Program* baseProg = context.program(baseValue.program());
            DeductionState state(baseProg, baseValue.program(), baseProg->parameters.size());
            state.identityMap(baseProg->inheritedParameterCount);
            popExpression();
            return { std::move(state) };
        } else if (baseValue.kind() == ValueKind::Parameterize) {
            auto param = program->getParameterize(baseValue);
            Program* baseProg = context.program(param.base);
            VERIFY(param.arguments.size() <= baseProg->parameters.size());
            DeductionState state(baseProg, param.base, param.arguments.size());
            for (int_t i = 0; i < (int_t)param.arguments.size(); i++)
                state.explicitArgument(i, param.arguments[i]);
            popExpression();
            return { std::move(state) };
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateCallExpr(CallTarget target, int_t argumentCount) {
    auto& state = target.state;
    if (state.program->kind() == ProgramKind::Function || state.program->kind() == ProgramKind::Type) {
        CallableProgram* callableProg = cast<CallableProgram>(state.program);

        VERIFY(argumentCount == (int_t)callableProg->runtimeParameters.size());
        for (int_t index = 0; index < argumentCount; index++) {
            const auto& parameter = callableProg->runtimeParameters[index];
            Expression argument = topExpression(argumentCount - 1 - index);
            implicitCastTo(state, parameter.type(), argument);
        }
        VERIFY(state.isComplete());
        Value callTarget = makeParameterize(state.programHandle, state.arguments);
        Type returnType = verifyType(fold(callTarget, callableProg->returnType()));
        emitExpression(Opcode::Call, SourceLocation(), argumentCount, returnType, { .callTarget = callTarget });
        return;
    }
    VERIFY_NOT_REACHED();
}

std::optional<Value> Generator::lookupInType(TypeProgram* typeProg, std::span<const Value> arguments, Word name) {
    auto maybeDecl = typeProg->getDeclaration(name);
    if (maybeDecl.has_value())
        return generateDeclarationLiteral(maybeDecl.value(), arguments);

    std::optional<Value> result;
    for (const auto& member : typeProg->runtimeParameters) {
        if (member.kind() != RuntimeParameterKind::HasMember)
            continue;
        Type baseType = member.type();
        if (baseType.kind() == ValueKind::Program || baseType.kind() == ValueKind::Parameterize) {
            FoldBase base = asFoldBase(baseType);
            auto maybeValue = lookupInType(cast<TypeProgram>(base.program), base.arguments, name);
            if (maybeValue.has_value()) {
                VERIFY(!result.has_value());
                result = maybeValue;
            }
        }
    }
    return result;
}

void Generator::generateStaticAccessExpr() {
    Word name = Word::fromUint(tok->data());
    Value baseValue = makeExpressionValue();
    if (baseValue.kind() == ValueKind::Namespace) {
        Namespace* ns = context.getNamespace(baseValue.nsHandle());
        emitConstantExpr(tok->location(), generateDeclarationLiteral(ns->getDeclaration(name).value(), {}));
        return;
    }
    if (baseValue.kind() == ValueKind::Program || baseValue.kind() == ValueKind::Parameterize) {
        FoldBase base = asFoldBase(baseValue);
        auto maybeValue = lookupInType(cast<TypeProgram>(base.program), base.arguments, name);
        VERIFY(maybeValue.has_value());
        return emitConstantExpr(tok->location(), maybeValue.value());
    }
    VERIFY_NOT_REACHED();
}

struct Generator::MemberAccessState {
    std::vector<uint32_t> memberIndicies;
    bool emitted = false;
};

void Generator::emitMemberAccessExpr(MemberAccessState& state) {
    VERIFY(!state.emitted);
    state.emitted = true;
    auto origExpr = topExpression();
    Type parentType = origExpr.type();
    if (origExpr.category() == InstructionCategory::PValue) {
        emitExpression(Opcode::PureInstantiation, tok->location(), 1, parentType, { .empty {} });
        emitMemberAccessExpr(state);
        emitExpression(Opcode::Purify, tok->location(), 1, topExpression().type(), {});
        VERIFY_NOT_REACHED();
    }
    Opcode opcode = origExpr.category() == InstructionCategory::LValue ? Opcode::LMemberAccess : Opcode::RMemberAccess;
    for (uint32_t memberIndex : state.memberIndicies) {
        MemberPointer memberPointer { parentType, memberIndex };
        Type mType = memberType(memberPointer);
        emitExpression(opcode, tok->location(), 1, mType, { .memberPointer = program->addMemberPointer(memberPointer) });
        parentType = mType;
    }
}

void Generator::generateMemberAccessExprInside(MemberAccessState& state, Type baseType, Word name) {
    if (baseType.kind() != ValueKind::Program && baseType.kind() != ValueKind::Parameterize)
        return;

    auto base = asFoldBase(baseType);
    VERIFY(base.program->kind() == ProgramKind::Type);
    TypeProgram* prog = cast<TypeProgram>(base.program);
    const auto& members = prog->runtimeParameters;
    for (int_t i = 0; i < (int_t)members.size(); i++) {
        if (members[i].name == name) {
            state.memberIndicies.push_back(i);
            emitMemberAccessExpr(state);
            return;
        }
    }
    for (int_t i = 0; i < (int_t)members.size(); i++) {
        if (members[i].kind() == RuntimeParameterKind::HasMember) {
            state.memberIndicies.push_back(i);
            generateMemberAccessExprInside(state, members[i].type(), name);
            state.memberIndicies.pop_back();
        }
    }
}

void Generator::generateMemberAccessExpr() {
    Word name = Word::fromUint(tok->data());
    Type baseType = topExpression().type();
    MemberAccessState state;
    generateMemberAccessExprInside(state, baseType, name);
    VERIFY(state.emitted);
}

void Generator::contextualType() {
    auto state = selfDeduction();
    implicitCastTo(state, builtins::type_type);
}

void Generator::contextualBool() {
    auto state = selfDeduction();
    implicitCastTo(state, builtins::bool_type);
}

void Generator::implicitCastTo(DeductionState& state, ExternValue pType, Expression arg) {
    bool sameType = staticMatch(state, pType, arg.type());
    VERIFY(sameType);
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
        Program* baseProg = context.program(param.base);
        if (baseProg->parameters.size() != param.arguments.size())
            return std::nullopt;
        return FoldBase { context.program(param.base), param.base, base, asVector(param.arguments) };
    }
    return std::nullopt;
}

FoldBase Generator::selfFold() {
    FoldBase base {
        program,
        programHandle,
        INVALID_VALUE,
        identityParameterMap(program),
    };
    return base;
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
        if (state.program == program && pValue == state.arguments[index]) {
            // TODO: This needs to be here to break recursion. But is it correct?
            state.expressionMatches.push_back({ pValue, aValue });
            return true;
        }
        auto selfState = selfDeduction();
        bool result = staticMatch(selfState, state.arguments[index], aValue);
        state.expressionMatches.insert(state.expressionMatches.end(), selfState.expressionMatches.begin(), selfState.expressionMatches.end());
        return result;
    }

    if (pValue.kind() == ValueKind::Expression || pValue.kind() == ValueKind::RemoteExpression
        || aValue.kind() == ValueKind::Expression || aValue.kind() == ValueKind::RemoteExpression
        || aValue.kind() == ValueKind::Parameter) {
        // TODO: check that the parameter-side value does not contain any non-explicit arguments
        state.expressionMatches.push_back({ pValue, aValue });
        return true;
    }

    auto comparePrograms = [&state](ProgramHandle pProg, ProgramHandle aProg) {
        return state.program->translate(pProg) == aProg;
    };
    auto compareParameterize = [this, &comparePrograms, &state](ExternValue pValue, Value aValue) {
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
    };

    if (pValue.kind() != aValue.kind())
        return false;
    switch (pValue.kind()) {
    case ValueKind::Program:
        return comparePrograms(Value(pValue).program(), aValue.program());
    case ValueKind::Namespace:
        return state.program->translate(Value(pValue).nsHandle()) == aValue.nsHandle();
    case ValueKind::TemplateSignature$Program:
        return comparePrograms(Value(pValue).templateSignatureProgram(), aValue.templateSignatureProgram()); // TODO: different programs can have the same signature
    case ValueKind::FunctionSignature$Program:
        return comparePrograms(Value(pValue).functionSignatureProgram(), aValue.functionSignatureProgram()); // TODO: different programs can have the same signature
    case ValueKind::TemplateSignature$Parameterize:
        return compareParameterize(Value(pValue).templateSignatureBaseValue(), aValue.templateSignatureBaseValue());
    case ValueKind::FunctionSignature$Parameterize:
        // TODO: different programs can have the same signature
        return compareParameterize(Value(pValue).functionSignatureBaseValue(), aValue.functionSignatureBaseValue());
    case ValueKind::Parameterize:
        return compareParameterize(pValue, aValue);
    case ValueKind::MemberPointer: {
        auto pMember = state.program->getMemberPointer(pValue);
        auto aMember = program->getMemberPointer(aValue);
        if (!staticMatch(state, pMember.parentType, aMember.parentType))
            return false;
        return pMember.memberIndex == aMember.memberIndex;
    }
    case ValueKind::BooleanLiteral:
        return Value(pValue).booleanValue() == aValue.booleanValue();
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
        return (Value)foldProgram(Value(v).program());
    case ValueKind::Parameter:
        return base.arguments[v.id()];
    case ValueKind::Namespace:
        return (Value)base.program->translate(Value(v).nsHandle());
    case ValueKind::TemplateSignature$Program:
    case ValueKind::TemplateSignature$Parameterize:
        return makeTemplateSignature(fold(base, Value(v).templateSignatureBaseValue()));
    case ValueKind::FunctionSignature$Program:
    case ValueKind::FunctionSignature$Parameterize:
        return makeFunctionSignature(fold(base, Value(v).functionSignatureBaseValue()));
    case ValueKind::RemoteExpression: {
        RemoteExpression expr = base.program->getRemoteExpression(v);
        return program->addRemoteExpression(fold(base, expr.base), expr.expression);
    }
    case ValueKind::Expression:
        return program->addRemoteExpression(base.value, v);
    case ValueKind::Parameterize: {
        auto externPara = base.program->getParameterize(v);
        std::vector<Value> foldedArgs;
        for (auto arg : externPara.arguments)
            foldedArgs.push_back(fold(base, arg));
        return program->addParameterize(foldProgram(externPara.base), foldedArgs);
    }
    case ValueKind::MemberPointer: {
        auto externMember = base.program->getMemberPointer(v);
        return program->addMemberPointer(verifyType(fold(base, externMember.parentType)), externMember.memberIndex);
    }
    case ValueKind::BooleanLiteral:
        return (Value)v;
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
    g.inheriteParameters(program->parent());
    // built lookup stack
    ScopeValue scope = program->parent();
    for (;;) {
        if (scope.kind() == ValueKind::Program) {
            Program* scopeProg = context.program(scope.program());
            g.lookupStack.push_back(LookupContext::forType(cast<TypeProgram>(scopeProg)));
            scope = scopeProg->translate(scopeProg->parent());
        } else if (scope.kind() == ValueKind::Namespace) {
            Namespace* scopeNS = context.getNamespace(scope.nsHandle());
            g.lookupStack.push_back(LookupContext::forNamespace(scopeNS));
            if (!scopeNS->parent.has_value())
                break;
            scope = scopeNS->parent.value();
        } else
            VERIFY_NOT_REACHED();
    }
    std::reverse(g.lookupStack.begin(), g.lookupStack.end());

    auto parseLocation = program->beginSignatureCheck();

    {
        ParseScope parseScope(&g, parseLocation);
        g.visitDeclaration();
    }

    program->completeSignatureCheck();
}

RuntimeParameter Generator::member(MemberPointer pointer) {
    return cast<TypeProgram>(asFoldBase(pointer.parentType).program)->runtimeParameters[pointer.memberIndex];
}

Type Generator::memberType(MemberPointer pointer) {
    auto base = asFoldBase(pointer.parentType);
    auto* prog = cast<TypeProgram>(base.program);
    return verifyType(fold(std::move(base), prog->runtimeParameters[pointer.memberIndex].type()));
}

Type Generator::typeOf(Value value) {
    switch (value.kind()) {
    case ValueKind::TemplateSignature$Program:
    case ValueKind::TemplateSignature$Parameterize:
        return builtins::template_signature_type;
    case ValueKind::FunctionSignature$Program:
    case ValueKind::FunctionSignature$Parameterize:
        return builtins::function_signature_type;
    case ValueKind::Namespace:
        return builtins::namespace_type;
    case ValueKind::BooleanLiteral:
        return builtins::bool_type;
    case ValueKind::Expression:
        return program->getExpression(value).type();
    case ValueKind::RemoteExpression: {
        auto rExpr = program->getRemoteExpression(value);
        auto base = asFoldBase(rExpr.base);
        return verifyType(fold(base, base.program->getExpression(rExpr.expression).type()));
    }
    case ValueKind::Parameter:
        return parameterTypes[value.id()];
    case ValueKind::Parameterize: {
        auto para = program->getParameterize(value);
        Program* baseProg = context.program(para.base);
        if (baseProg->parameters.size() == para.arguments.size())
            return typeOfNonDependentProgram(value);
        return makeTemplateIdFor(value);
    }
    case ValueKind::Program: {
        Program* prog = context.program(value.program());
        if (!prog->isDependent())
            return typeOfNonDependentProgram(value);
        return makeTemplateIdFor(value);
    }
    case ValueKind::MemberPointer: {
        MemberPointer pointer = program->getMemberPointer(value);
        std::array<Value, 2> arguments { pointer.parentType, memberType(pointer) };
        return verifyType(makeParameterize(builtins::member_ptr_template.program(), arguments));
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Type Generator::typeOfNonDependentProgram(Value value) {
    return typeOfNonDependentProgram(asFoldBase(value));
}

Type Generator::typeOfNonDependentProgram(FoldBase base) {
    switch (base.program->kind()) {
    case ProgramKind::Function: {
        std::array arguments { makeFunctionSignature(base.value) };
        return verifyType(program->addParameterize(builtins::function_id_template.program(), arguments));
    }
    case ProgramKind::Object: {
        std::array arguments { (Value)cast<ObjectProgram>(base.program)->objectType() };
        return verifyType(program->addParameterize(builtins::ptr_template.program(), arguments));
    }
    case ProgramKind::Value:
        return verifyType(fold(base, cast<ValueProgram>(base.program)->type()));
    case ProgramKind::Type:
        return builtins::type_type;
    default:
        VERIFY_NOT_REACHED();
    }
}

Type Generator::verifyType(Value value) {
    switch (value.kind()) {
    case ValueKind::Program: {
        Program* valueProg = context.program(value.program());
        VERIFY(!valueProg->isTemplate());
        switch (valueProg->kind()) {
        case ProgramKind::Object:
        case ProgramKind::Function:
            VERIFY_NOT_REACHED();
        case ProgramKind::Type:
            return (Type)value;
        case ProgramKind::Value:
            VERIFY(cast<ValueProgram>(valueProg)->type() == builtins::type_type);
            return (Type)value;
        default:
            VERIFY_NOT_REACHED();
        }
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
        VERIFY(value == (ScopeValue)Value(id));
        return value.program();
    }

    BuiltinGenerator(Context& context, BuiltinId id)
        : Generator(context, createScope(context, id)) {
        program->beginSignatureCheck();
    }

    ~BuiltinGenerator() {
        program->setType(verifyType(makeParameterize(programHandle, identityParameterMap(program))));
        program->completeSignatureCheck();
        context.popScope();
    }
};

void Generator::generateBuiltins(Context& context) {
    {
        BuiltinGenerator g { context, BuiltinId::type_type };
    }
    {
        BuiltinGenerator g { context, BuiltinId::bool_type };
    }
    {
        BuiltinGenerator g { context, BuiltinId::error_type };
    }
    {
        BuiltinGenerator g { context, BuiltinId::namespace_type };
    }

    // typeof(tempalte(T: type) => expr) = template_id{template(T: type) -> typeof(expr)}
    // cast{type}(template(T: type) -> type_expr) = template_id{template(T: type) -> type_expr}

    // template(sig: template_signature) struct template_id: { }
    // typof(template_id) = typeof(template(sig: template_signature) => template_id{sig})
    //                    = template_id{template(sig: template_signature) -> typeof(template_id{sig})}
    //                    = template_id{template(sig: template_signature) -> type}
    {
        BuiltinGenerator g { context, BuiltinId::template_signature_type };
    }
    {
        BuiltinGenerator g { context, BuiltinId::template_id_template };
        g.addExplicitParameter(parse::words["sig"], builtins::template_signature_type, {});
    }

    // template(sig: function_signature) struct function_id: { }
    // typeof(function_id) = typeof(template(sig: function_signature) => function_id{sig})
    //                     = template_id{template(sig: function_signature) -> typeof(function_id{sig})}
    //                     = template_id{template(sig: function_signature) -> type}
    {
        BuiltinGenerator g { context, BuiltinId::function_signature_type };
    }
    {
        BuiltinGenerator g { context, BuiltinId::function_id_template };
        g.addExplicitParameter(parse::words["sig"], builtins::function_signature_type, {});
    }

    // template(pointee_type: type) struct ptr: { }
    {
        BuiltinGenerator g { context, BuiltinId::ptr_template };
        g.addExplicitParameter(parse::words["pointee_type"], builtins::type_type, {});
    }

    // template(parent_type: type, member_type: type) struct member_ptr: { }
    {
        BuiltinGenerator g { context, BuiltinId::member_ptr_template };
        g.addExplicitParameter(parse::words["parent_type"], builtins::type_type, {});
        g.addExplicitParameter(parse::words["member_type"], builtins::type_type, {});
    }

    // cast{template_id}( template_function_id{template(T: type) fn(t: T) -> T)} )
    //   = template_id{ template(T: type) -> function_id{fn(t: T) -> T} }

    // template(T: type) fn(t: T) -> T = template(T: type) -> function_id{fn(t: T) -> T}

    // typeof( (arg = expr) ) = cast{type}( (arg = typeof(expr)) ) = tuple{cast{tuple_signature}( (arg = typeof(expr)) )}
}

}