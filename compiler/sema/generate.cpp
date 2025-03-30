#include <sema/Context.h>
#include <sema/Generator.h>

#include <ranges>

namespace sema {

Expression Generator::topExpression() {
    return currentExpression;
}

OwnedExpression Generator::takeTopExpression() {
    VERIFY(!expressionStack.empty());
    expressionStack.pop_back();
    return std::move(currentExpression);
}

void Generator::emitExpression(SourceLocation, OwnedExpression e) {
    VERIFY(currentExpression == INVALID_EXPRESSION);
    currentExpression = std::move(e);
    expressionStack.push_back({ .endOffset = (uint32_t)instructionScratch.size() });
}

void Generator::emitCall(SourceLocation location, Call call) {
    VERIFY(currentExpression == INVALID_EXPRESSION);
    currentExpression = program->addCall(call);
    instructionScratch.emplace_back(Opcode::Call, location, Instruction::Data { .callExpression = currentExpression });
    expressionStack.push_back({ .endOffset = (uint32_t)instructionScratch.size() });
}

void Generator::emitImplicitCopy(SourceLocation location, ImplicitCopy copy) {
    VERIFY(currentExpression == INVALID_EXPRESSION);
    currentExpression = program->addImplicitCopy(copy);
    instructionScratch.emplace_back(Opcode::ImplicitCopy, location, Instruction::Data { .implicitCopyExpression = currentExpression });
    expressionStack.push_back({ .endOffset = (uint32_t)instructionScratch.size() });
}

Constant Generator::expressionToConstant() {
    VERIFY(!expressionStack.empty());
    toValueExpression(SourceLocation());

    if (topExpression().isConstant())
        return takeTopExpression().constant();

    VERIFY(expressionStack.size() >= 2);
    VERIFY(expressionStack.back().endOffset == instructionScratch.size());
    VERIFY(std::prev(expressionStack.end(), 2)->endOffset < instructionScratch.size()); // expect a non-zero number of instructions
    auto newEnd = instructionScratch.begin() + std::prev(expressionStack.end(), 2)->endOffset;

    OwnedExpression input = std::move(currentExpression);
    Type type = resultType(input);
    Constant result = program->addComputedConstant(context, { input.release(), type, { newEnd, instructionScratch.end() } });

    instructionScratch.erase(newEnd, instructionScratch.end());
    expressionStack.pop_back();
    VERIFY(expressionStack.back().endOffset == instructionScratch.size());

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
        auto para = program->getParameterize(value);
        VERIFY(para.arguments.size() == context.program(para.base)->parameters.size());
        return Constant(ConstantKind::FunctionSignature$Parameterize, value.id());
    }
    VERIFY_NOT_REACHED();
}

Expression Generator::makeGlobalReference(Constant value) {
    if (value.kind() == ConstantKind::Program)
        return Expression(ExpressionKind::GlobalReference$Program, value.id());
    if (value.kind() == ConstantKind::Parameterize)
        return Expression(ExpressionKind::GlobalReference$Parameterize, value.id());
    VERIFY_NOT_REACHED();
}

Constant Generator::makeCopyOfOpenGlobal(Constant value) {
    if (value.kind() == ConstantKind::Program)
        return Constant(ConstantKind::CopyOfOpenGlobal$Program, value.id());
    if (value.kind() == ConstantKind::Parameterize)
        return Constant(ConstantKind::CopyOfOpenGlobal$Parameterize, value.id());
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
        parameterTypes[i] = type;
    }
    return parentValue;
}

std::optional<Constant> Generator::resolveImplicitImplTarget() {
    if (program->parent().kind() != ConstantKind::Program)
        return std::nullopt;
    ProgramHandle parentProgHandle = program->parent().program();
    Program* parentProg = context.program(parentProgHandle);
    if (!parentProg->isImpl())
        return std::nullopt;

    VERIFY(parentProg->kind() == ProgramKind::Type);
    Constant parentImplOf = fold(makeParameterize(parentProgHandle, copyParameters(parentProg)), parentProg->selfConstant());
    auto parentImplPara = program->getParameterize(parentImplOf);

    auto implTarget = cast<TypeProgram>(context.program(parentImplPara.base))->getDeclaration(program->name());
    VERIFY(implTarget.has_value());
    VERIFY(implTarget.value().kind() == ConstantKind::Program);
    ProgramHandle implOfProgHandle = implTarget.value().program();
    signatureCheck(context, implOfProgHandle);
    Program* implOfProg = context.program(implOfProgHandle);

    VERIFY(program->kind() == implOfProg->kind());
    DeductionState state(implOfProg, implOfProgHandle, implOfProg->parameters.size());
    VERIFY(implOfProg->inheritedParameterCount == parentImplPara.arguments.size());
    for (int_t i = 0; i < (int_t)implOfProg->inheritedParameterCount; i++)
        state.explicitArgument(i, parentImplPara.arguments[i]);

    // Match explicit parameter against each over
    int_t parameterIndex = program->inheritedParameterCount;
    int_t implParameterIndex = implOfProg->inheritedParameterCount;
    for (;; parameterIndex++, implParameterIndex++) {
        // Find next explict parameters
        while (parameterIndex < (int_t)program->parameters.size() && program->parameters[parameterIndex].implicit())
            parameterIndex += 1;
        while (implParameterIndex < (int_t)implOfProg->parameters.size() && implOfProg->parameters[implParameterIndex].implicit())
            implParameterIndex += 1;
        if (parameterIndex == (int_t)program->parameters.size()) {
            VERIFY(implParameterIndex == (int_t)implOfProg->parameters.size());
            break;
        }
        VERIFY(implParameterIndex < (int_t)implOfProg->parameters.size());

        auto parameter = program->parameters[parameterIndex];
        auto implParameter = implOfProg->parameters[implParameterIndex];
        VERIFY(parameter.name == implParameter.name);
        bool match = staticMatch(state, implParameter.type, (Constant)parameter.type);
        VERIFY(match);
        state.explicitArgument(implParameterIndex, Constant(ConstantKind::CopyOfParameter, parameterIndex));
        // TODO: What about the initializer?
    }

    VERIFY(state.isComplete());
    return makeParameterize(implOfProgHandle, state.arguments);
}

Expression Generator::generateDeclarationLiteral(ScopeConstant rawValue, std::span<const Constant> baseArgs) {
    if (rawValue.kind() == ConstantKind::Namespace) {
        VERIFY(baseArgs.empty());
        return (Constant)rawValue.nsHandle();
    }

    VERIFY(rawValue.kind() == ConstantKind::Program);
    ProgramHandle progHandle = rawValue.program();
    signatureCheck(context, progHandle);
    return generateProgramLiteral(progHandle, baseArgs);
}

Expression Generator::generateProgramLiteral(ProgramHandle progHandle, std::span<const Constant> args) {
    Program* prog = context.program(progHandle);
    signatureCheck(context, progHandle);
    Constant progValue = makeParameterize(progHandle, args);
    switch (prog->kind()) {
    case ProgramKind::Type:
    case ProgramKind::Function:
        return progValue;
    case ProgramKind::Global: {
        if (prog->isTemplate() && (int_t)args.size() < prog->parameterizes.size())
            return progValue;
        return makeGlobalReference(progValue);
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

void Generator::generateIdentifierExpr() {
    Word name = Word::fromUint(tok->data());
    if (name == parse::words["false"]) {
        emitExpression(tok->location(), builtins::false_constant);
        return;
    }
    if (name == parse::words["true"]) {
        emitExpression(tok->location(), builtins::true_constant);
        return;
    }
    for (auto lookupCtx : std::views::reverse(lookupStack)) {
        switch (lookupCtx.kind()) {
        case LookupContext::Kind::Namespace: {
            auto result = lookupCtx.getNamespace()->getDeclaration(name);
            if (result.has_value()) {
                emitExpression(tok->location(), generateDeclarationLiteral(result.value(), {}));
                return;
            }
            continue;
        }
        case LookupContext::Kind::ContainingType: {
            TypeProgram* prog = lookupCtx.getContainingType();
            auto result = lookupInType(prog, copyParameters(prog), name);
            if (result.has_value()) {
                emitExpression(tok->location(), result.value());
                return;
            }
            for (int_t i = prog->inheritedParameterCount; i < (int_t)prog->parameters.size(); i++) {
                if (prog->parameters[i].name == name) {
                    emitExpression(tok->location(), Expression::templateParameterReference(i));
                    return;
                }
            }
            continue;
        };
        case LookupContext::Kind::TemplateParameters: {
            Program* prog = lookupCtx.getTemplateParameters();
            for (int_t i = prog->inheritedParameterCount; i < (int_t)prog->parameters.size(); i++) {
                if (prog->parameters[i].name == name) {
                    emitExpression(tok->location(), Expression::templateParameterReference(i));
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
                    emitExpression(tok->location(), entry.data);
                    return;
                }
            }
            continue;
        }
        case LookupContext::Kind::ContainingTypeImpl: {
            Constant implOf = lookupCtx.getContainingTypeImpl();
            auto para = program->getParameterize(implOf);
            auto result = lookupInType(cast<TypeProgram>(context.program(para.base)), para.arguments, name);
            if (result.has_value()) {
                emitExpression(tok->location(), result.value());
                return;
            }
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }
    fmt::println("Failed to lookup '{}'", context.wordTable.view(name));
    VERIFY_NOT_REACHED();
}

void Generator::generateParameterizeExpr(std::span<const Word> argumentNames) {
    Expression baseResult = topExpression();
    if (baseResult.isConstant()) {
        Constant baseValue = baseResult.constant();
        takeTopExpression();

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
                initialize(conversionLocation, state, ExpressionCategory::Value, pType);
                state.explicitArgument(pIndex, expressionToConstant());
            }
            VERIFY(tok->kind() == Token::EmptyNode);
            advance();

            VERIFY(aIndex == argumentCount);
            VERIFY(pIndex == parameterCount);
            VERIFY(state.isComplete());
            emitExpression({}, generateProgramLiteral(state.programHandle, state.arguments));
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
    auto baseResult = topExpression();

    // Ugly special case
    if (baseResult.kind() == ExpressionKind::GlobalReference$Program || baseResult.kind() == ExpressionKind::GlobalReference$Parameterize) {
        const auto base = asFoldBase(baseResult.referencedGlobal());
        auto* globalProg = cast<GlobalProgram>(base.program);
        if (globalProg->globalKind() == GlobalKind::Let)
            baseResult = fold(base, globalProg->initializer());
    }

    if (baseResult.isConstant()) {
        auto baseValue = baseResult.constant();
        if (baseValue.kind() == ConstantKind::Program) {
            Program* baseProg = context.program(baseValue.program());
            VERIFY(cast<CallableProgram>(baseProg)->runtimeParameters.size() == argumentNames.size());
            DeductionState state(baseProg, baseValue.program(), baseProg->parameters.size());
            state.copyParameters(baseProg->inheritedParameterCount);
            takeTopExpression();
            return { std::move(state) };
        } else if (baseValue.kind() == ConstantKind::Parameterize) {
            auto param = program->getParameterize(baseValue);
            Program* baseProg = context.program(param.base);
            VERIFY(cast<CallableProgram>(baseProg)->runtimeParameters.size() == argumentNames.size());
            VERIFY(param.arguments.size() <= baseProg->parameters.size());
            DeductionState state(baseProg, param.base, param.arguments.size());
            for (int_t i = 0; i < (int_t)param.arguments.size(); i++)
                state.explicitArgument(i, param.arguments[i]);
            takeTopExpression();
            return { std::move(state) };
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateCallExpr(SourceLocation location, CallTarget target) {
    auto& state = target.state;
    if (state.program->kind() == ProgramKind::Function || state.program->kind() == ProgramKind::Type) {
        CallableProgram* callableProg = cast<CallableProgram>(state.program);

        std::vector<Expression> arguments;
        arguments.resize(callableProg->runtimeParameters.size(), INVALID_EXPRESSION);

        int_t argumentIndex = 0;
        const auto& parameters = callableProg->runtimeParameters;
        while (tok->kind() == Token::CallArgument) {
            SourceLocation conversionLocation = tok->location();
            advance();
            visitExpression();
            auto expectedCategory = expectedInitializerCategory(callableProg->runtimeParameters[argumentIndex].kind());
            initialize(conversionLocation, state, expectedCategory, parameters[argumentIndex].type());
            arguments[argumentIndex] = takeTopExpression().release();
            argumentIndex += 1;
        }
        VERIFY(tok->kind() == Token::EmptyNode);
        advance();

        VERIFY(state.isComplete());
        Constant callTarget = makeParameterize(state.programHandle, state.arguments);
        Type returnType = verifyType(callableProg->kind() == ProgramKind::Type ? callTarget : fold(callTarget, cast<FunctionProgram>(callableProg)->returnType()));
        emitCall(location, Call { ExpressionCategory::Value, callTarget, returnType, arguments });
        return;
    }
    VERIFY_NOT_REACHED();
}

std::optional<Expression> Generator::lookupInType(TypeProgram* typeProg, std::span<const Constant> arguments, Word name) {
    VERIFY(arguments.size() == typeProg->parameters.size());
    auto maybeDecl = typeProg->getDeclaration(name);
    if (maybeDecl.has_value())
        return generateDeclarationLiteral(maybeDecl.value(), arguments);

    std::optional<Expression> result;
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
    Constant baseValue = expressionToConstant();
    if (baseValue.kind() == ConstantKind::Namespace) {
        Namespace* ns = context.getNamespace(baseValue.nsHandle());
        emitExpression(tok->location(), generateDeclarationLiteral(ns->getDeclaration(name).value(), {}));
        return;
    }
    if (baseValue.kind() == ConstantKind::Program || baseValue.kind() == ConstantKind::Parameterize) {
        FoldBase base = asFoldBase(baseValue);
        auto maybeValue = lookupInType(cast<TypeProgram>(base.program), base.arguments, name);
        VERIFY(maybeValue.has_value());
        return emitExpression(tok->location(), maybeValue.value());
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
    auto forEachMemberPointer = [this, origType = resultType(topExpression()), &state](auto callback) {
        Type parentType = origType;
        for (uint32_t memberIndex : state.memberIndicies) {
            MemberPointer memberPointer { parentType, memberIndex };
            Type mType = memberType(memberPointer);
            callback(mType, memberPointer);
            parentType = mType;
        }
    };

    auto expr = takeTopExpression();
    forEachMemberPointer([&](Type, MemberPointer pointer) {
        expr = program->addMemberExpression({ std::move(expr), program->addMemberPointer(context, pointer) });
    });
    emitExpression(tok->location(), std::move(expr));
    return;
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
    Type baseType = resultType(topExpression());
    MemberAccessState state;
    generateMemberAccessExprInside(state, baseType, name);
    VERIFY(state.emitted);
}

Constant Generator::copyAsConstant(Expression expr) {
    switch (expr.kind()) {
    case ExpressionKind::TemplateParameterReference:
        return expr.copyTemplateParameter();
    case ExpressionKind::GlobalReference$Program:
    case ExpressionKind::GlobalReference$Parameterize: {
        FoldBase base = asFoldBase(expr.referencedGlobal());
        VERIFY(base.program->kind() == ProgramKind::Global);
        return fold(base, cast<GlobalProgram>(base.program)->initializer());
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

void Generator::toValueExpression(SourceLocation location) {
    auto inCategory = categoryOf(topExpression());
    if (inCategory == ExpressionCategory::Value)
        return;

    auto copyFrom = takeTopExpression();
    switch (copyFrom.kind()) {
    case ExpressionKind::GlobalReference$Program:
    case ExpressionKind::GlobalReference$Parameterize: {
        FoldBase base = asFoldBase(copyFrom.referencedGlobal());
        VERIFY(base.program->kind() == ProgramKind::Global);
        auto* globalProg = cast<GlobalProgram>(base.program);
        if (globalProg->globalKind() == GlobalKind::Let) {
            emitExpression(location, fold(base, globalProg->initializer()));
            return;
        } else if (globalProg->globalKind() == GlobalKind::OpenLet) {
            emitExpression(location, makeCopyOfOpenGlobal(base.value));
            return;
        }
        break;
    }
    case ExpressionKind::TemplateParameterReference:
        emitExpression(location, copyFrom.copyTemplateParameter());
        return;
    default:
        break;
    }
    emitImplicitCopy(location, ImplicitCopy { copyFrom, resultType(copyFrom) });
}

void Generator::contextualToType(SourceLocation location) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, programHandle, 0);
    initialize(location, state, ExpressionCategory::Value, builtins::type_type);
}

void Generator::contextualToBool(SourceLocation location) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, programHandle, 0);
    initialize(location, state, ExpressionCategory::Value, builtins::bool_type);
}

void Generator::initialize(SourceLocation location, DeductionState& state, ExpressionCategory expectedCategory, ExternConstant expectedType) {
    auto expr = topExpression();
    bool sameType = staticMatch(state, expectedType, resultType(expr));
    VERIFY(sameType);
    auto inputCategory = categoryOf(expr);
    if (expectedCategory == ExpressionCategory::Value) {
        toValueExpression(location);
    } else {
        VERIFY(inputCategory != ExpressionCategory::Value); // initializing a reference with a value requires making temporary

        if (expectedCategory != inputCategory) {
            // only down casts allowed
            if (expectedCategory == ExpressionCategory::SharedReference || expectedCategory == ExpressionCategory::ConstUniqueReference)
                VERIFY(inputCategory == ExpressionCategory::UniqueReference);
            else
                VERIFY(expectedCategory == ExpressionCategory::ConstSharedReference);
        }
    }
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
    if (pValue == builtins::self_constant)
        pValue = state.program->selfConstant(); // Will always be a parameterize or program constant

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

    if (pValue.kind() == ConstantKind::Computed || pValue.kind() == ConstantKind::RemoteComputed
        || aValue.kind() == ConstantKind::Computed || aValue.kind() == ConstantKind::RemoteComputed
        || pValue.kind() == ConstantKind::CopyOfOpenGlobal$Program || pValue.kind() == ConstantKind::CopyOfOpenGlobal$Parameterize
        || aValue.kind() == ConstantKind::CopyOfOpenGlobal$Program || aValue.kind() == ConstantKind::CopyOfOpenGlobal$Parameterize
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
    if (v == builtins::self_constant)
        v = base.program->selfConstant();
    switch (v.kind()) {
    case ConstantKind::Program:
        return (Constant)foldProgram(Constant(v).program());
    case ConstantKind::CopyOfParameter:
        // TODO: Actually perform a copy?
        return base.arguments[v.id()];
    case ConstantKind::CopyOfOpenGlobal$Program:
    case ConstantKind::CopyOfOpenGlobal$Parameterize:
        return makeCopyOfOpenGlobal(fold(base, Constant(v).copiedGlobal()));
    case ConstantKind::Namespace:
        return (Constant)base.program->translate(Constant(v).nsHandle());
    case ConstantKind::TemplateSignature$Program:
    case ConstantKind::TemplateSignature$Parameterize:
        return makeTemplateSignature(fold(base, Constant(v).templateSignatureBaseConstant()));
    case ConstantKind::FunctionSignature$Program:
    case ConstantKind::FunctionSignature$Parameterize:
        return makeFunctionSignature(fold(base, Constant(v).functionSignatureBaseConstant()));
    case ConstantKind::RemoteComputed: {
        RemoteComputation com = base.program->getRemoteComputedConstant(v);
        return program->addRemoteComputedConstant(context, { fold(base, com.base), com.computation });
    }
    case ConstantKind::Computed:
        if (base.value == builtins::self_constant)
            return (Constant)v; // Must not contain any temporary template parameters
        return program->addRemoteComputedConstant(context, { base.value, v });
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
            if (scopeProg->isImpl()) {
                // TODO: Find a nicer way to do this. Maybe it is not necessary to eagerly fold the impl expression?
                Constant parentPara = g.makeParameterize(scope.program(), copyParameters(scopeProg));
                g.lookupStack.push_back(LookupContext::forContainingTypeImpl(g.fold(parentPara, scopeProg->selfConstant())));
            } else {
                // TODO: Even when this is an impl, there can be private members in this program.
                g.lookupStack.push_back(LookupContext::forContainingType(cast<TypeProgram>(scopeProg)));
            }
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
}

ExpressionCategory Generator::categoryOf(Expression expr) {
    switch (expr.kind()) {
    case ExpressionKind::GlobalReference$Program:
    case ExpressionKind::GlobalReference$Parameterize: {
        auto base = asFoldBase(expr.referencedGlobal());
        switch (cast<GlobalProgram>(base.program)->globalKind()) {
        case GlobalKind::Var:
            return ExpressionCategory::SharedReference;
        case GlobalKind::ConstVar:
        case GlobalKind::Let:
        case GlobalKind::OpenLet:
            return ExpressionCategory::ConstSharedReference;
        default:
            VERIFY_NOT_REACHED();
        }
    }
    case ExpressionKind::TemplateParameterReference:
        return ExpressionCategory::ConstSharedReference;
    case ExpressionKind::VariableReference:
        return ExpressionCategory::UniqueReference;
    case ExpressionKind::ParameterReference:
        switch (cast<FunctionProgram>(program)->runtimeParameters[expr.id()].kind()) {
        case RuntimeParameterKind::VarVariable:
            return ExpressionCategory::UniqueReference;
        case RuntimeParameterKind::LetVariable:
            return ExpressionCategory::ConstUniqueReference;
        case RuntimeParameterKind::UniqueReference:
            return ExpressionCategory::UniqueReference;
        case RuntimeParameterKind::ConstUniqueReference:
            return ExpressionCategory::ConstUniqueReference;
        case RuntimeParameterKind::SharedReference:
            return ExpressionCategory::SharedReference;
        case RuntimeParameterKind::ConstSharedReference:
            return ExpressionCategory::ConstSharedReference;
        default:
            VERIFY_NOT_REACHED();
        }
    case ExpressionKind::ReferenceReference:
        return localReferences[expr.referenceIndex()].category;
    case ExpressionKind::MemberExpression:
        return categoryOf(program->getMemberReference(expr).base);
    case ExpressionKind::Call:
        return program->getCall(expr).resultCategory;
    case ExpressionKind::ImplicitCopy:
        return ExpressionCategory::Value;
    default:
        VERIFY(expr.isConstant());
        return ExpressionCategory::Value;
    }
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
    case ConstantKind::Computed:
        return program->getComputedConstant(value).type;
    case ConstantKind::RemoteComputed: {
        auto rExpr = program->getRemoteComputedConstant(value);
        auto base = asFoldBase(rExpr.base);
        return verifyType(fold(base, base.program->getComputedConstant(rExpr.computation).type));
    }
    case ConstantKind::CopyOfParameter:
        return parameterTypes[value.id()];
    case ConstantKind::CopyOfOpenGlobal$Program:
    case ConstantKind::CopyOfOpenGlobal$Parameterize: {
        auto base = asFoldBase(value.copiedGlobal());
        return verifyType(fold(base, cast<GlobalProgram>(base.program)->type()));
    }
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

Type Generator::resultType(Expression expr) {
    if (expr.isConstant())
        return typeOf(expr.constant());

    switch (expr.kind()) {
    case ExpressionKind::GlobalReference$Program:
    case ExpressionKind::GlobalReference$Parameterize: {
        FoldBase base = asFoldBase(expr.referencedGlobal());
        VERIFY(base.program->kind() == ProgramKind::Global);
        return verifyType(fold(base, cast<GlobalProgram>(base.program)->type()));
    }
    case ExpressionKind::TemplateParameterReference:
        return parameterTypes[expr.templateParameterIndex()];
    case ExpressionKind::ParameterReference:
        return cast<FunctionProgram>(program)->runtimeParameters[expr.parameterIndex()].type();
    case ExpressionKind::VariableReference:
        return localVariables[expr.variableIndex()].type;
    case ExpressionKind::ReferenceReference:
        return localReferences[expr.referenceIndex()].type;
    case ExpressionKind::MemberExpression: {
        auto memberExpr = program->getMemberReference(expr);
        return memberType(memberExpr.memberPointer);
    }
    case ExpressionKind::Call:
        return program->getCall(expr).returnType;
    case ExpressionKind::ImplicitCopy:
        return program->getImplicitCopy(expr).type;
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
    case ProgramKind::Global:
        return verifyType(fold(base, cast<GlobalProgram>(base.program)->type()));
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
        case ProgramKind::Function:
            VERIFY_NOT_REACHED();
        case ProgramKind::Type:
            return (Type)value;
        case ProgramKind::Global:
            VERIFY(cast<GlobalProgram>(valueProg)->type() == builtins::type_type);
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

Expression Generator::addParameter(Word name, Type type, std::optional<Constant> defaultValue) {
    VERIFY(parameterTypes.size() == program->parameters.size());
    uint32_t parameterIndex = program->parameters.size();
    parameterTypes.push_back(type);
    program->parameters.push_back({ name, type, defaultValue });
    return Expression::templateParameterReference(parameterIndex);
}

Expression Generator::addExplicitParameter(Word name, Type type, std::optional<Constant> defaultValue) {
    return addParameter(name, type, defaultValue);
}

Expression Generator::newImplicitParameter(Type type) {
    uint32_t parameterIndex = parameterTypes.size();
    parameterTypes.push_back(type);
    return Expression::templateParameterReference(parameterIndex);
}

Expression Generator::addInheritedParameter(Type type, std::optional<Constant> defaultValue) {
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
        Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
        program->completeSignatureCheck(false, selfConstant);
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