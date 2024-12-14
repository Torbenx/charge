#include <sema/Context.h>
#include <sema/Generator.h>

#include <ranges>

namespace sema {

Instruction& Generator::topInstruction(int_t n) {
    auto entry = *(expressionStack.end() - n - 1);
    return instructionScratch[entry.endOffset - 1];
}

Expression Generator::topExpression(int_t n) {
    return topInstruction(n);
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

Generator::JumpLabel Generator::nextInstruction() {
    return { (uint32_t)instructionScratch.size(), localState };
}

Generator::JumpReference Generator::emitJump(Opcode op, SourceLocation location, int_t childCount) {
    uint32_t offset = instructionScratch.size();
    emitControl(op, location, childCount, { .jumpDistance = 0 });
    return { offset, localState };
}

void Generator::linkToNextInstruction(const JumpReference& jump) {
    int_t targetOffset = instructionScratch.size();
    link(jump.offset, targetOffset, jump.originState, localState);
}

void Generator::emitJumpTo(Opcode op, SourceLocation location, int_t childCount, const JumpLabel& label) {
    int_t originOffset = instructionScratch.size();
    emitControl(op, location, childCount, { .jumpDistance = 0 });
    link(originOffset, label.offset, localState, label.targetState);
}

void Generator::link(int_t originOffset, int_t targetOffset, const LocalState& originState, const LocalState& targetState) {
    VERIFY(originState == targetState);
    auto& jumpInst = instructionScratch[originOffset];
    VERIFY(jumpInst.u.jumpDistance == 0);
    jumpInst.u.jumpDistance = targetOffset - originOffset;
}

void Generator::emitExpression(Opcode op, SourceLocation location, int_t childCount, Type type, ExpressionData data) {
    VERIFY(expressionStack.back().endOffset == instructionScratch.size());
    expressionStack.resize(expressionStack.size() - childCount);
    instructionScratch.emplace_back(op, location, InstructionData { .expr { type, data } });
    expressionStack.push_back({ .endOffset = (uint32_t)instructionScratch.size() });
}

void Generator::emitConstantExpr(SourceLocation location, Constant value) {
    emitExpression(Opcode::Constant, location, 0, typeOf(value), { .constant = value });
}

void Generator::emitReferenceExpr(SourceLocation location, ReferenceExpression expr) {
    return emitExpression(
        Opcode::Reference, location, 0, referencedType(expr),
        { .referenceExpr = expr });
}

Constant Generator::makeExpressionConstant() {
    if (topExpression().opcode() == Opcode::Constant) {
        Constant result = topExpression().data().constant;
        VERIFY((expressionStack.end() - 2)->endOffset == instructionScratch.size() - 1);
        instructionScratch.pop_back();
        expressionStack.pop_back();
        return result;
    }
    auto newEnd = instructionScratch.begin() + (expressionStack.end() - 2)->endOffset;
    Constant result = program->addExpression({ newEnd, instructionScratch.end() });
    instructionScratch.erase(newEnd, instructionScratch.end());
    expressionStack.pop_back();
    return result;
}

Constant Generator::makeTemplateSignature(Constant templateProg) {
    if (templateProg.kind() == ConstantKind::Program)
        return Constant(ConstantKind::TemplateSignature$Program, templateProg.id());
    if (templateProg.kind() == ConstantKind::Parameterize)
        return Constant(ConstantKind::TemplateSignature$Parameterize, templateProg.id());
    VERIFY_NOT_REACHED();
}

Constant Generator::makeFunctionSignature(Constant value) {
    if (value.kind() == ConstantKind::Program) {
        VERIFY(!context.program(value.program())->isDependent());
        return Constant(ConstantKind::FunctionSignature$Program, value.id());
    }
    if (value.kind() == ConstantKind::Parameterize) {
        return Constant(ConstantKind::FunctionSignature$Parameterize, value.id());
    }
    VERIFY_NOT_REACHED();
}

Type Generator::makeTemplateIdFor(Constant templateProg) {
    std::array arguments { makeTemplateSignature(templateProg) };
    return verifyType(program->addParameterize(context, { builtins::template_id_template.program(), arguments }));
}

Constant Generator::makeParameterize(ProgramHandle base, std::span<const Constant> arguments) {
    Program* baseProg = context.program(base);
    VERIFY(arguments.size() == baseProg->inheritedParameterCount || arguments.size() == baseProg->parameters.size());
    if (arguments.empty())
        return Constant(base);
    return program->addParameterize(context, { base, arguments });
}

Constant Generator::inheriteParameters(ScopeConstant parent) {
    if (parent.kind() == ConstantKind::Namespace)
        return (Constant)parent.nsHandle();

    VERIFY(parent.kind() == ConstantKind::Program);

    ProgramHandle parentHandle = parent.program();
    signatureCheck(context, parentHandle);
    Program* parentProg = context.program(parentHandle);
    if (!parentProg->isDependent())
        return (Constant)parentHandle;

    // inherite parameters
    int_t parameterCount = parentProg->parameters.size();
    VERIFY(parameterCount > 0);
    std::vector<Constant> parentArguments(parameterCount, INVALID_CONSTANT);
    for (int_t i = 0; i < parameterCount; i++)
        parentArguments[i] = addInheritedParameter((Type)INVALID_CONSTANT, std::nullopt).copyTemplateParameter();

    Constant parentValue = program->addParameterize(context, { parentHandle, parentArguments });
    FoldBase base = asFoldBase(parentValue);
    for (int_t i = 0; i < parameterCount; i++) {
        Type type = verifyType(fold(base, base.program->parameters[i].type));
        program->parameters[i].type = type;
    }
    return parentValue;
}

Constant Generator::generateDeclarationLiteral(ScopeConstant rawValue, std::span<const Constant> baseArgs) {
    auto makeProgramConstant = [this, baseArgs](ProgramHandle targetHandle) {
        Program* targetProg = context.program(targetHandle);
        VERIFY(targetProg->inheritedParameterCount <= baseArgs.size());
        if (targetProg->inheritedParameterCount > 0)
            return program->addParameterize(context, { targetHandle, baseArgs });
        return Constant(targetHandle);
    };

    if (rawValue.kind() == ConstantKind::Namespace)
        return (Constant)rawValue.nsHandle();
    VERIFY(rawValue.kind() == ConstantKind::Program);

    ProgramHandle progHandle = rawValue.program();
    Program* prog = context.program(progHandle);
    signatureCheck(context, progHandle);
    switch (prog->kind()) {
    case ProgramKind::Object:
    case ProgramKind::Type:
    case ProgramKind::Function:
        return makeProgramConstant(progHandle);
    case ProgramKind::Value: {
        Constant progValue = makeProgramConstant(progHandle);
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
        emitConstantExpr(tok->location(), Constant(ConstantKind::BooleanLiteral, 0));
        return;
    }
    if (name == parse::words["true"]) {
        emitConstantExpr(tok->location(), Constant(ConstantKind::BooleanLiteral, 1));
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
            auto result = lookupInType(prog, copyParameters(prog), name);
            if (result.has_value()) {
                emitConstantExpr(tok->location(), result.value());
                return;
            }
            for (int_t i = prog->inheritedParameterCount; i < (int_t)prog->parameters.size(); i++) {
                if (prog->parameters[i].name == name) {
                    emitReferenceExpr(tok->location(), ReferenceExpression(ReferenceExpressionKind::TemplateParameter, i));
                    return;
                }
            }
            continue;
        };
        case LookupContext::Kind::TemplateParameters: {
            Program* prog = lookupCtx.getTemplateParameters();
            for (int_t i = prog->inheritedParameterCount; i < (int_t)prog->parameters.size(); i++) {
                if (prog->parameters[i].name == name) {
                    emitReferenceExpr(tok->location(), ReferenceExpression(ReferenceExpressionKind::TemplateParameter, i));
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
                    if (entry.data.isReference())
                        emitReferenceExpr(tok->location(), entry.data.reference());
                    else
                        emitConstantExpr(tok->location(), entry.data.constant());
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

void Generator::generateParameterizeExpr(std::span<const Word> argumentNames) {
    Expression baseExpr = topExpression();
    if (baseExpr.opcode() == Opcode::Constant) {
        Constant baseValue = baseExpr.data().constant;
        popExpression();

        auto generate = [this, argumentCount = (int_t)argumentNames.size()](DeductionState state) {
            int_t parameterCount = state.arguments.size();
            int_t pIndex = state.program->inheritedParameterCount;
            int_t aIndex = 0;
            for (; tok->kind() == Token::CallArgument; aIndex++, pIndex++) {
                SourceLocation conversionLocation = tok->location();
                advance();

                // Find next explicit parameter
                while (pIndex < parameterCount && state.program->parameters[pIndex].implicit())
                    pIndex += 1;
                VERIFY(pIndex < parameterCount);

                ExternConstant pType = state.program->parameters[pIndex].type;
                visitExpression();
                implicitCastTo(conversionLocation, state, pType);
                state.explicitArgument(pIndex, makeExpressionConstant());
            }
            VERIFY(tok->kind() == Token::EmptyNode);
            advance();

            VERIFY(aIndex == argumentCount);
            VERIFY(pIndex == parameterCount);
            VERIFY(state.isComplete());
            Constant result = makeParameterize(state.programHandle, state.arguments);
            if (state.program->kind() == ProgramKind::Value)
                result = fold(result, cast<ValueProgram>(state.program)->value());

            emitConstantExpr({}, result);
        };

        if (baseValue.kind() == ConstantKind::Program) {
            Program* baseProg = context.program(baseValue.program());
            VERIFY(baseProg->isTemplate());
            VERIFY(baseProg->inheritedParameterCount == 0);
            DeductionState state(baseProg, baseValue.program(), baseProg->parameters.size());
            generate(std::move(state));
            return;
        }
        if (baseValue.kind() == ConstantKind::Parameterize) {
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

Generator::CallTarget Generator::resolveCallTarget(std::span<const Word> argumentNames) {
    auto baseExpr = topExpression();
    if (baseExpr.opcode() == Opcode::Constant) {
        auto baseValue = baseExpr.data().constant;
        if (baseValue.kind() == ConstantKind::Program) {
            Program* baseProg = context.program(baseValue.program());
            VERIFY(cast<CallableProgram>(baseProg)->runtimeParameters.size() == argumentNames.size());
            DeductionState state(baseProg, baseValue.program(), baseProg->parameters.size());
            state.copyParameters(baseProg->inheritedParameterCount);
            popExpression();
            return { std::move(state) };
        } else if (baseValue.kind() == ConstantKind::Parameterize) {
            auto param = program->getParameterize(baseValue);
            Program* baseProg = context.program(param.base);
            VERIFY(cast<CallableProgram>(baseProg)->runtimeParameters.size() == argumentNames.size());
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

void Generator::generateCallExpr(CallTarget target) {
    auto& state = target.state;
    if (state.program->kind() == ProgramKind::Function || state.program->kind() == ProgramKind::Type) {
        CallableProgram* callableProg = cast<CallableProgram>(state.program);

        int_t argumentIndex = 0;
        const auto& parameters = callableProg->runtimeParameters;
        while (tok->kind() == Token::CallArgument) {
            SourceLocation conversionLocation = tok->location();
            advance();
            visitExpression();
            implicitCastTo(conversionLocation, state, parameters[argumentIndex].type());
            argumentIndex += 1;
        }
        VERIFY(tok->kind() == Token::EmptyNode);
        advance();

        VERIFY(state.isComplete());
        Constant callTarget = makeParameterize(state.programHandle, state.arguments);
        Type returnType = verifyType(callableProg->kind() == ProgramKind::Type ? callTarget : fold(callTarget, cast<FunctionProgram>(callableProg)->returnType()));
        emitExpression(Opcode::Call, SourceLocation(), parameters.size(), returnType, { .callTarget = callTarget });
        return;
    }
    VERIFY_NOT_REACHED();
}

std::optional<Constant> Generator::lookupInType(TypeProgram* typeProg, std::span<const Constant> arguments, Word name) {
    auto maybeDecl = typeProg->getDeclaration(name);
    if (maybeDecl.has_value())
        return generateDeclarationLiteral(maybeDecl.value(), arguments);

    std::optional<Constant> result;
    for (const auto& member : typeProg->runtimeParameters) {
        if (member.kind() != RuntimeParameterKind::HasMember)
            continue;
        Type baseType = member.type();
        if (baseType.kind() == ConstantKind::Program || baseType.kind() == ConstantKind::Parameterize) {
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
    Constant baseValue = makeExpressionConstant();
    if (baseValue.kind() == ConstantKind::Namespace) {
        Namespace* ns = context.getNamespace(baseValue.nsHandle());
        emitConstantExpr(tok->location(), generateDeclarationLiteral(ns->getDeclaration(name).value(), {}));
        return;
    }
    if (baseValue.kind() == ConstantKind::Program || baseValue.kind() == ConstantKind::Parameterize) {
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
    auto forEachMemberPointer = [this, origType = origExpr.type(), &state](auto callback) {
        Type parentType = origType;
        for (uint32_t memberIndex : state.memberIndicies) {
            MemberPointer memberPointer { parentType, memberIndex };
            Type mType = memberType(memberPointer);
            callback(mType, memberPointer);
            parentType = mType;
        }
    };

    if (origExpr.opcode() == Opcode::Reference) {
        popExpression();
        ReferenceExpression reference = origExpr.data().referenceExpr;
        forEachMemberPointer([&](Type, MemberPointer pointer) {
            reference = program->addMemberReferenceExpression({ reference, program->addMemberPointer(context, pointer) });
        });
        emitReferenceExpr(tok->location(), reference);
        return;
    }

    VERIFY(origExpr.category() == InstructionCategory::RValue);
    forEachMemberPointer([&](Type memberType, MemberPointer pointer) {
        emitExpression(Opcode::RMemberAccess, tok->location(), 1, memberType, { .memberPointer = program->addMemberPointer(context, pointer) });
    });
}

void Generator::generateMemberAccessExprInside(MemberAccessState& state, Type baseType, Word name) {
    if (baseType.kind() != ConstantKind::Program && baseType.kind() != ConstantKind::Parameterize)
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

void Generator::makeRValue(SourceLocation location) {
    auto expr = topExpression();
    if (expr.category() == InstructionCategory::RValue)
        return;

    if (expr.opcode() == Opcode::Reference) {
        auto ref = expr.data().referenceExpr;
        if (ref.kind() == ReferenceExpressionKind::TemplateParameter) {
            popExpression();
            emitConstantExpr(expr.location(), Constant(ConstantKind::CopyOfParameter, ref.templateParameterIndex()));
            return;
        }
    }

    emitExpression(Opcode::ImplicitCopy, location, 1, expr.type(), { .empty {} });
}

void Generator::contextualToType(SourceLocation location) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, programHandle, 0);
    implicitCastTo(location, state, builtins::type_type);
}

void Generator::contextualToBool(SourceLocation location) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, programHandle, 0);
    implicitCastTo(location, state, builtins::bool_type);
}

void Generator::implicitCastTo(SourceLocation location, DeductionState& state, ExternConstant pType) {
    bool sameType = staticMatch(state, pType, topExpression().type());
    makeRValue(location);
    VERIFY(sameType);
}

FoldBase Generator::asFoldBase(Constant base) {
    return tryAsFoldBase(base).value();
}

std::optional<FoldBase> Generator::tryAsFoldBase(Constant base) {
    if (base.kind() == ConstantKind::Program) {
        Program* baseProg = context.program(base.program());
        if (baseProg->isDependent())
            return std::nullopt;
        return FoldBase { baseProg, base.program(), base, {} };
    } else if (base.kind() == ConstantKind::Parameterize) {
        auto param = program->getParameterize(base);
        Program* baseProg = context.program(param.base);
        if (baseProg->parameters.size() != param.arguments.size())
            return std::nullopt;
        return FoldBase { context.program(param.base), param.base, base, param.arguments };
    }
    return std::nullopt;
}

// pValue and aValue must be known to have the same type
bool Generator::staticMatch(DeductionState& state, ExternConstant pValue, Constant aValue) {
    if (pValue.kind() == ConstantKind::CopyOfParameter) {
        int_t index = pValue.id();
        if (state.arguments[index] == INVALID_CONSTANT) {
            state.arguments[index] = aValue;
            VERIFY(!state.isExplicitArgument(index));
        } else {
            // TODO: In many cases this could determined right here by comparing state.arguments[index] and aValue.
            //       This would require either a separate compare function or recursion with some weird DeductionState.
            state.equalities.add(context, program, state.program, { pValue, aValue });
        }
        return true;
    }

    if (pValue.kind() == ConstantKind::Expression || pValue.kind() == ConstantKind::RemoteExpression
        || aValue.kind() == ConstantKind::Expression || aValue.kind() == ConstantKind::RemoteExpression
        || aValue.kind() == ConstantKind::CopyOfParameter) {
        // TODO: check that the parameter-side value does not contain any non-explicit arguments
        state.equalities.add(context, program, state.program, { pValue, aValue });
        return true;
    }

    auto comparePrograms = [&state](ProgramHandle pProg, ProgramHandle aProg) {
        return state.program->translate(pProg) == aProg;
    };
    auto compareParameterize = [this, &comparePrograms, &state](ExternConstant pValue, Constant aValue) {
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
    case ConstantKind::Program:
        return comparePrograms(Constant(pValue).program(), aValue.program());
    case ConstantKind::Namespace:
        return state.program->translate(Constant(pValue).nsHandle()) == aValue.nsHandle();
    case ConstantKind::TemplateSignature$Program:
        return comparePrograms(Constant(pValue).templateSignatureProgram(), aValue.templateSignatureProgram()); // TODO: different programs can have the same signature
    case ConstantKind::FunctionSignature$Program:
        return comparePrograms(Constant(pValue).functionSignatureProgram(), aValue.functionSignatureProgram()); // TODO: different programs can have the same signature
    case ConstantKind::TemplateSignature$Parameterize:
        return compareParameterize(Constant(pValue).templateSignatureBaseConstant(), aValue.templateSignatureBaseConstant());
    case ConstantKind::FunctionSignature$Parameterize:
        // TODO: different programs can have the same signature
        return compareParameterize(Constant(pValue).functionSignatureBaseConstant(), aValue.functionSignatureBaseConstant());
    case ConstantKind::Parameterize:
        return compareParameterize(pValue, aValue);
    case ConstantKind::MemberPointer: {
        auto pMember = state.program->getMemberPointer(pValue);
        auto aMember = program->getMemberPointer(aValue);
        if (!staticMatch(state, pMember.parentType, aMember.parentType))
            return false;
        return pMember.memberIndex == aMember.memberIndex;
    }
    case ConstantKind::BooleanLiteral:
        return Constant(pValue).booleanValue() == aValue.booleanValue();
    default:
        VERIFY_NOT_REACHED();
    }
}

Constant Generator::fold(Constant base, ExternConstant v) {
    return fold(asFoldBase(base), v);
}

Constant Generator::fold(FoldBase base, ExternConstant v) {
    auto foldProgram = [&base](ProgramHandle handle) {
        return base.program->translate(handle);
    };
    switch (v.kind()) {
    case ConstantKind::Program:
        return (Constant)foldProgram(Constant(v).program());
    case ConstantKind::CopyOfParameter:
        // TODO: Actually perform a copy?
        return base.arguments[v.id()];
    case ConstantKind::Namespace:
        return (Constant)base.program->translate(Constant(v).nsHandle());
    case ConstantKind::TemplateSignature$Program:
    case ConstantKind::TemplateSignature$Parameterize:
        return makeTemplateSignature(fold(base, Constant(v).templateSignatureBaseConstant()));
    case ConstantKind::FunctionSignature$Program:
    case ConstantKind::FunctionSignature$Parameterize:
        return makeFunctionSignature(fold(base, Constant(v).functionSignatureBaseConstant()));
    case ConstantKind::RemoteExpression: {
        RemoteExpression expr = base.program->getRemoteExpression(v);
        return program->addRemoteExpression(context, { fold(base, expr.base), expr.expression });
    }
    case ConstantKind::Expression:
        return program->addRemoteExpression(context, { base.value, v });
    case ConstantKind::Parameterize: {
        auto externPara = base.program->getParameterize(v);
        std::vector<Constant> foldedArgs;
        for (auto arg : externPara.arguments)
            foldedArgs.push_back(fold(base, arg));
        return program->addParameterize(context, { foldProgram(externPara.base), foldedArgs });
    }
    case ConstantKind::MemberPointer: {
        auto externMember = base.program->getMemberPointer(v);
        return program->addMemberPointer(context, { verifyType(fold(base, externMember.parentType)), externMember.memberIndex });
    }
    case ConstantKind::BooleanLiteral:
        return (Constant)v;
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
    // build lookup stack
    ScopeConstant scope = program->parent();
    for (;;) {
        if (scope.kind() == ConstantKind::Program) {
            Program* scopeProg = context.program(scope.program());
            g.lookupStack.push_back(LookupContext::forType(cast<TypeProgram>(scopeProg)));
            scope = scopeProg->translate(scopeProg->parent());
        } else if (scope.kind() == ConstantKind::Namespace) {
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
    g.setParseLocation(parseLocation);
    g.visitDeclaration();
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

Type Generator::memberType(Constant memberPointerValue) {
    if (memberPointerValue.kind() == ConstantKind::MemberPointer)
        return memberType(program->getMemberPointer(memberPointerValue));

    auto memberPointerType = typeOf(memberPointerValue);
    VERIFY(memberPointerType.kind() == ConstantKind::Parameterize);

    auto para = program->getParameterize(memberPointerType);
    VERIFY(para.base == builtins::member_ptr_template.program());
    VERIFY(para.arguments.size() == 2);

    return verifyType(para.arguments[1]);
}

Type Generator::typeOf(Constant value) {
    switch (value.kind()) {
    case ConstantKind::TemplateSignature$Program:
    case ConstantKind::TemplateSignature$Parameterize:
        return builtins::template_signature_type;
    case ConstantKind::FunctionSignature$Program:
    case ConstantKind::FunctionSignature$Parameterize:
        return builtins::function_signature_type;
    case ConstantKind::Namespace:
        return builtins::namespace_type;
    case ConstantKind::BooleanLiteral:
        return builtins::bool_type;
    case ConstantKind::Expression:
        return program->getExpression(value).type();
    case ConstantKind::RemoteExpression: {
        auto rExpr = program->getRemoteExpression(value);
        auto base = asFoldBase(rExpr.base);
        return verifyType(fold(base, base.program->getExpression(rExpr.expression).type()));
    }
    case ConstantKind::CopyOfParameter:
        return parameterTypes[value.id()];
    case ConstantKind::Parameterize: {
        auto para = program->getParameterize(value);
        Program* baseProg = context.program(para.base);
        if (baseProg->parameters.size() == para.arguments.size())
            return typeOfNonDependentProgram(value);
        return makeTemplateIdFor(value);
    }
    case ConstantKind::Program: {
        Program* prog = context.program(value.program());
        if (!prog->isDependent())
            return typeOfNonDependentProgram(value);
        return makeTemplateIdFor(value);
    }
    case ConstantKind::MemberPointer: {
        MemberPointer pointer = program->getMemberPointer(value);
        std::array<Constant, 2> arguments { pointer.parentType, memberType(pointer) };
        return verifyType(makeParameterize(builtins::member_ptr_template.program(), arguments));
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

Type Generator::typeOfNonDependentProgram(Constant value) {
    return typeOfNonDependentProgram(asFoldBase(value));
}

Type Generator::typeOfNonDependentProgram(FoldBase base) {
    switch (base.program->kind()) {
    case ProgramKind::Function: {
        std::array arguments { makeFunctionSignature(base.value) };
        return verifyType(program->addParameterize(context, { builtins::function_id_template.program(), arguments }));
    }
    case ProgramKind::Object: {
        std::array arguments { (Constant)cast<ObjectProgram>(base.program)->objectType() };
        return verifyType(program->addParameterize(context, { builtins::ptr_template.program(), arguments }));
    }
    case ProgramKind::Value:
        return verifyType(fold(base, cast<ValueProgram>(base.program)->type()));
    case ProgramKind::Type:
        return builtins::type_type;
    default:
        VERIFY_NOT_REACHED();
    }
}

Type Generator::verifyType(Constant value) {
    switch (value.kind()) {
    case ConstantKind::Program: {
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

Type Generator::referencedType(ReferenceExpression expr) {
    switch (expr.kind()) {
    case ReferenceExpressionKind::Parameter:
        return cast<FunctionProgram>(program)->runtimeParameters[expr.parameterIndex()].type();
    case ReferenceExpressionKind::TemplateParameter:
        return parameterTypes[expr.templateParameterIndex()];
    case ReferenceExpressionKind::LocalVariable:
        return localVariables[expr.localVaraibleIndex()].type;
    case ReferenceExpressionKind::LocalReference:
        return localReferences[expr.localReferenceIndex()].type;
    case ReferenceExpressionKind::MemberExpression: {
        auto memberExpr = program->getMemberReferenceExpression(expr);
        return memberType(memberExpr.memberPointer);
    }
    case ReferenceExpressionKind::OpaqueExpression:
        return opaqueReferenceExpressions[expr.opaqueExpressionIndex()].type;
    default:
        VERIFY_NOT_REACHED();
    }
}

ReferenceExpression Generator::addParameter(Word name, Type type, std::optional<Constant> defaultValue) {
    VERIFY(parameterTypes.size() == program->parameters.size());
    uint32_t parameterIndex = program->parameters.size();
    parameterTypes.push_back(type);
    program->parameters.push_back({ name, type, defaultValue });
    return ReferenceExpression(ReferenceExpressionKind::TemplateParameter, parameterIndex);
}

ReferenceExpression Generator::addExplicitParameter(Word name, Type type, std::optional<Constant> defaultValue) {
    return addParameter(name, type, defaultValue);
}

ReferenceExpression Generator::newImplicitParameter(Type type) {
    uint32_t parameterIndex = parameterTypes.size();
    parameterTypes.push_back(type);
    return ReferenceExpression(ReferenceExpressionKind::TemplateParameter, parameterIndex);
}

ReferenceExpression Generator::addInheritedParameter(Type type, std::optional<Constant> defaultValue) {
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
        VERIFY(value == (ScopeConstant)Constant(id));
        return value.program();
    }

    BuiltinGenerator(Context& context, BuiltinId id)
        : Generator(context, createScope(context, id)) {
        program->beginSignatureCheck();
    }

    ~BuiltinGenerator() {
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