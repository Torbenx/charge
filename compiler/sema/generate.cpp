#include <sema/Context.h>
#include <sema/Generator.h>

#include <ranges>

namespace sema {

void Generator::resolveLazyExpressions() {
    if (currentExpression.kind() == ExpressionKind::LazyParameterize) {
        VERIFY(lazyParameterizeState.has_value());
        VERIFY(lazyParameterizeState.value().isComplete());
        currentExpression = generateProgramLiteral(lazyParameterizeState.value().programHandle, lazyParameterizeState.value().arguments);
        lazyParameterizeState.reset();
    }
}

Expression Generator::topExpression() {
    resolveLazyExpressions();
    return currentExpression;
}

OwnedExpression Generator::takeTopExpression() {
    VERIFY(!expressionStack.empty());
    expressionStack.pop_back();
    resolveLazyExpressions();
    return std::move(currentExpression);
}

DeductionState Generator::takeLazyParameterize() {
    VERIFY(currentExpression.kind() == ExpressionKind::LazyParameterize);
    VERIFY(lazyParameterizeState.has_value());
    DeductionState result = std::move(lazyParameterizeState.value());
    lazyParameterizeState.reset();
    currentExpression = INVALID_EXPRESSION;
    return result;
}

bool Generator::isTopExpressionLazyParameterize() {
    return currentExpression.kind() == ExpressionKind::LazyParameterize;
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

Constant Generator::inheriteParameters(DeclarationValue parent) {
    if (parent.kind() == DeclarationValueKind::Namespace)
        return (Constant)parent.nsHandle();

    VERIFY(parent.kind() == DeclarationValueKind::Program);

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

bool Generator::resolveImplicitImplTarget() {
    if (program->parent().kind() != DeclarationValueKind::Program)
        return false;
    ProgramHandle parentProgHandle = program->parent().program();
    Program* parentProg = context.program(parentProgHandle);
    if (!parentProg->isImpl())
        return false;

    VERIFY(try_cast<ScopeProgram>(parentProg).has_value());
    Constant parentImplOf = fold(makeParameterize(parentProgHandle, copyParameters(parentProg)), parentProg->selfConstant());
    auto parentImplPara = program->getParameterize(parentImplOf);

    auto implTarget = cast<ScopeProgram>(context.program(parentImplPara.base))->getDeclaration(program->name());
    VERIFY(implTarget.has_value());
    VERIFY(implTarget.value().kind() == DeclarationValueKind::Program);
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

    lazyParameterizeState = std::move(state);
    emitExpression({}, Expression(ExpressionKind::LazyParameterize, 0));
    return true;
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
                initialize(conversionLocation, state, Constant(ExpressionCategory::Value), pType);
                state.explicitArgument(pIndex, expressionToConstant());
            }
            VERIFY(tok->kind() == Token::EmptyNode);
            advance();

            VERIFY(aIndex == argumentCount);
            lazyParameterizeState = std::move(state);
            emitExpression({}, Expression(ExpressionKind::LazyParameterize, 0));
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
    Expression baseResult = INVALID_EXPRESSION;
    if (isTopExpressionLazyParameterize()) {
        DeductionState state = takeLazyParameterize();
        if (state.program->kind() == ProgramKind::Global) {
            VERIFY(state.isComplete());
            baseResult = generateProgramLiteral(state.programHandle, state.arguments);
        } else {
            // VERIFY(cast<CallableProgram>(state.program)->runtimeParameters.size() == argumentNames.size());
            return { std::move(state) };
        }
    } else
        baseResult = topExpression();

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
            // VERIFY(cast<CallableProgram>(baseProg)->runtimeParameters.size() == argumentNames.size());
            DeductionState state(baseProg, baseValue.program(), baseProg->parameters.size());
            state.copyParameters(baseProg->inheritedParameterCount);
            takeTopExpression();
            return { std::move(state) };
        } else if (baseValue.kind() == ConstantKind::Parameterize) {
            auto param = program->getParameterize(baseValue);
            Program* baseProg = context.program(param.base);
            // VERIFY(cast<CallableProgram>(baseProg)->runtimeParameters.size() == argumentNames.size());
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
    if (state.program->kind() == ProgramKind::Function || state.program->kind() == ProgramKind::Struct) {

        auto arguments = visit<callParameters>(state.program, [this, &state](auto parameters) { return generateCallArguments(state, parameters); });

        VERIFY(state.isComplete());
        Constant callTarget = makeParameterize(state.programHandle, state.arguments);
        Type returnType = verifyType(state.program->kind() == ProgramKind::Struct ? callTarget : fold(callTarget, cast<FunctionProgram>(state.program)->returnType()));
        emitCall(location, Call { Constant(ExpressionCategory::Value), callTarget, returnType, arguments });
        return;
    }
    VERIFY_NOT_REACHED();
}

template<std::ranges::random_access_range R>
std::vector<Expression> Generator::generateCallArguments(DeductionState& state, R parameters) {
    int_t parameterCount = std::ssize(parameters);
    std::vector<Expression> arguments;
    arguments.resize(parameterCount, INVALID_EXPRESSION);

    int_t argumentIndex = 0;
    while (tok->kind() == Token::CallArgument) {
        VERIFY(argumentIndex < parameterCount);
        CallParameter parameter = parameters[argumentIndex];
        SourceLocation conversionLocation = tok->location();
        advance();
        visitExpression();
        initialize(conversionLocation, state, parameter.expectedInitializerCategory, parameter.type);
        arguments[argumentIndex] = takeTopExpression().release();
        argumentIndex += 1;
    }
    VERIFY(argumentIndex == parameterCount);
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();

    return arguments;
}

MemberPointerData Generator::generateMemberPointer(Type originType, std::span<const uint32_t> memberIndices) {
    MemberPointerData result;
    result.m_data.push_back(originType.toUint());
    for (uint32_t memberIndex : memberIndices)
        extendMemberPointer(result, memberIndex);
    return result;
}

void Generator::extendMemberPointer(MemberPointerData& pointer, uint32_t memberIndex) {
    Type parentType = Type::fromUint(pointer.m_data.back());
    pointer.m_data.push_back(memberIndex);

    auto base = asFoldBase(parentType);
    const auto& members = cast<StructProgram>(base.program)->members;
    VERIFY(memberIndex < members.size());
    Type memberType = verifyType(fold(base, members[memberIndex].type()));
    pointer.m_data.push_back(memberType.toUint());
}

void Generator::internalLookupRecurse(InternalLookupState& state, ScopeProgram* prog) {
    auto maybeResult = prog->getDeclaration(state.lookupName);
    if (maybeResult.has_value()) {
        state.setResult(generateMemberPointer(state.originType, state.memberIndices), maybeResult.value());
        return;
    }

    if (prog->kind() != ProgramKind::Struct)
        return;

    auto* structProg = cast<StructProgram>(prog);
    for (int_t memberIndex = 0; memberIndex < (int_t)structProg->members.size(); memberIndex++) {
        const auto& member = structProg->members[memberIndex];
        if (member.name() == state.lookupName) {
            state.setResult(generateMemberPointer(state.originType, state.memberIndices), { DeclarationValueKind::Member, (uint32_t)memberIndex });
            return;
        }

        if (!member.isHas())
            continue;
        auto memberProg = structProg->baseProgram(member.type());
        if (!memberProg.has_value())
            continue;
        state.memberIndices.push_back(memberIndex);
        internalLookupRecurse(state, cast<ScopeProgram>(context.program(memberProg.value())));
        state.memberIndices.pop_back();
    }
}

Generator::InternalLookupResult Generator::internalLookup(Type type, Word name) {
    auto baseProg = baseProgram(type);
    if (!baseProg.has_value())
        return InternalLookupResult();

    InternalLookupState state(type, name);
    internalLookupRecurse(state, cast<ScopeProgram>(context.program(baseProg.value())));
    return state.result;
}

Expression Generator::generateDeclarationLiteral(InternalLookupResult result) {
    VERIFY(result.value.has_value());
    return generateDeclarationLiteral(result.value.value(), result.memberPointer().memberType());
}

Expression Generator::generateDeclarationLiteral(DeclarationValue rawValue, std::optional<Type> parent) {
    switch (rawValue.kind()) {
    case DeclarationValueKind::Namespace: {
        VERIFY(!parent.has_value());
        return (Constant)rawValue.nsHandle();
    }
    case DeclarationValueKind::Program: {
        ProgramHandle progHandle = rawValue.program();
        signatureCheck(context, progHandle);

        std::span<const Constant> parentArgs;
        if (parent.has_value())
            parentArgs = asFoldBase(parent.value()).arguments;
        return generateProgramLiteral(progHandle, parentArgs);
    }
    case DeclarationValueKind::EnumValue: {
        VERIFY(context.program(baseProgram(parent.value()).value())->kind() == ProgramKind::Enum);
        return program->addEnumValue(context, { parent.value(), rawValue.id() });
    }
    default:
        // Members are handled in each calling context individually
        VERIFY_NOT_REACHED();
    }
}

Expression Generator::generateProgramLiteral(ProgramHandle progHandle, std::span<const Constant> args) {
    Program* prog = context.program(progHandle);
    signatureCheck(context, progHandle);
    Constant progValue = makeParameterize(progHandle, args);
    switch (prog->kind()) {
    case ProgramKind::Struct:
    case ProgramKind::Function:
    case ProgramKind::Enum:
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
                emitExpression(tok->location(), generateDeclarationLiteral(result.value(), std::nullopt));
                return;
            }
            continue;
        }
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
        case LookupContext::Kind::ContainingType: {
            Type type = lookupCtx.getContainingType();
            auto result = internalLookup(type, name);
            if (result.value.has_value()) {
                VERIFY(result.value->kind() != DeclarationValueKind::Member); // Member should be looked up with .member or ::member
                emitExpression(tok->location(), generateDeclarationLiteral(std::move(result)));
                return;
            }
            auto base = asFoldBase(type);
            for (int_t i = base.program->inheritedParameterCount; i < (int_t)base.program->parameters.size(); i++) {
                if (base.program->parameters[i].name == name) {
                    emitExpression(tok->location(), Expression::templateParameterReference(i));
                    return;
                }
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

void Generator::generateStaticAccessExpr() {
    Word name = Word::fromUint(tok->data());
    Constant baseValue = expressionToConstant();
    if (baseValue.kind() == ConstantKind::Namespace) {
        Namespace* ns = context.getNamespace(baseValue.nsHandle());
        emitExpression(tok->location(), generateDeclarationLiteral(ns->getDeclaration(name).value(), std::nullopt));
        return;
    }
    if (baseValue.kind() == ConstantKind::Program || baseValue.kind() == ConstantKind::Parameterize) {
        FoldBase base = asFoldBase(baseValue);
        if (base.program->kind() == ProgramKind::Struct || base.program->kind() == ProgramKind::Enum) {
            auto result = internalLookup(verifyType(baseValue), name);
            VERIFY(result.value.has_value());
            if (result.value->kind() == DeclarationValueKind::Member) {
                extendMemberPointer(result.memberPointerData, result.value->id());
                emitExpression(tok->location(), program->addMemberPointer(context, result.memberPointerData));
            } else {
                emitExpression(tok->location(), generateDeclarationLiteral(std::move(result)));
            }
            return;
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateMemberAccessExpr() {
    auto baseExpr = takeTopExpression();
    Word name = Word::fromUint(tok->data());
    Type baseType = resultType(baseExpr);
    auto result = internalLookup(baseType, name);
    VERIFY(result.value.has_value());
    if (result.value->kind() == DeclarationValueKind::Member) {
        extendMemberPointer(result.memberPointerData, result.value->id());
        auto memberExpr = program->addMemberExpression({ baseExpr, program->addMemberPointer(context, result.memberPointerData) });
        emitExpression(tok->location(), memberExpr);
    } else {
        emitExpression(tok->location(), generateDeclarationLiteral(std::move(result)));
    }
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
    VERIFY(inCategory.kind() == ConstantKind::ExpressionCategoryLiteral); // TODO: Make this generic
    if (inCategory == Constant(ExpressionCategory::Value))
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
    initialize(location, state, Constant(ExpressionCategory::Value), builtins::type_type);
}

void Generator::contextualToBool(SourceLocation location) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, programHandle, 0);
    initialize(location, state, Constant(ExpressionCategory::Value), builtins::bool_type);
}

void Generator::contextualToExpressionCategory(SourceLocation location) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, programHandle, 0);
    initialize(location, state, Constant(ExpressionCategory::Value), builtins::expression_category_type);
}

void Generator::initialize(SourceLocation location, DeductionState& state, ExternConstant expectedCategoryConstant, ExternConstant expectedType) {
    auto expr = topExpression();
    bool sameType = staticMatch(state, expectedType, resultType(expr));
    VERIFY(sameType);
    auto inputCategoryConstant = categoryOf(expr);
    if (expectedCategoryConstant.kind() != ConstantKind::ExpressionCategoryLiteral) {
        bool match = staticMatch(state, expectedCategoryConstant, inputCategoryConstant);
        VERIFY(match);
        return;
    }

    VERIFY(inputCategoryConstant.kind() == ConstantKind::ExpressionCategoryLiteral); // TODO: Make this generic
    auto inputCategory = inputCategoryConstant.expressionCategory();
    auto expectedCategory = Constant(expectedCategoryConstant).expressionCategory();
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

std::optional<ProgramHandle> Generator::baseProgram(Constant value) {
    if (value.kind() == ConstantKind::Program) {
        return value.program();
    } else if (value.kind() == ConstantKind::Parameterize) {
        auto para = program->getParameterize(value);
        return para.base;
    } else {
        return std::nullopt;
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
        if (!staticMatch(state, pMember.originType(), aMember.originType()))
            return false;
        if (pMember.linkCount() != aMember.linkCount())
            return false;
        for (int_t linkIndex = 0; linkIndex < aMember.linkCount(); linkIndex++) {
            if (pMember[linkIndex].memberIndex != aMember[linkIndex].memberIndex)
                return false;
        }
        return true;
    }
    case ConstantKind::EnumValue: {
        // The types must be the same as a precondition
        auto pEnumValue = state.program->getEnumValue(pValue);
        auto aEnumValue = program->getEnumValue(aValue);
        return pEnumValue.valueIndex == aEnumValue.valueIndex;
    }
    case ConstantKind::BooleanLiteral:
        return Constant(pValue).booleanValue() == aValue.booleanValue();
    case ConstantKind::ExpressionCategoryLiteral:
        return Constant(pValue).expressionCategory() == aValue.expressionCategory();
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
        MemberPointerData member;
        member.m_data.push_back(fold(base, externMember.originType()).toUint());
        for (auto link : externMember) {
            member.m_data.push_back(link.memberIndex);
            member.m_data.push_back(link.memberType.toUint());
        }
        return program->addMemberPointer(context, member);
    }
    case ConstantKind::EnumValue: {
        auto enumValue = base.program->getEnumValue(v);
        return program->addEnumValue(context, { fold(base, enumValue.enumType), enumValue.valueIndex });
    }
    case ConstantKind::BooleanLiteral:
        return (Constant)v;
    case ConstantKind::ExpressionCategoryLiteral:
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
    DeclarationValue scope = program->parent();
    for (;;) {
        if (scope.kind() == DeclarationValueKind::Program) {
            Program* scopeProg = context.program(scope.program());
            Constant parentPara = g.makeParameterize(scope.program(), copyParameters(scopeProg));
            if (scopeProg->isImpl()) {
                g.lookupStack.push_back(LookupContext::forContainingType(g.verifyType(g.fold(parentPara, scopeProg->selfConstant()))));
            } else {
                // TODO: Even when this is an impl, there can be private members in this program.
                g.lookupStack.push_back(LookupContext::forContainingType(g.verifyType(parentPara)));
            }
            scope = scopeProg->translate(scopeProg->parent());
        } else if (scope.kind() == DeclarationValueKind::Namespace) {
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

Type Generator::memberType(Constant memberPointer) {
    if (memberPointer.kind() == ConstantKind::MemberPointer)
        return program->getMemberPointer(memberPointer).memberType();

    auto type = typeOf(memberPointer);
    VERIFY(type.kind() == ConstantKind::Parameterize);
    auto para = program->getParameterize(type);
    VERIFY(para.base == builtins::member_ptr_template.program());
    VERIFY(para.arguments.size() == 2);
    return verifyType(para.arguments[1]);
}

Constant Generator::categoryOf(Expression expr) {
    switch (expr.kind()) {
    case ExpressionKind::GlobalReference$Program:
    case ExpressionKind::GlobalReference$Parameterize: {
        auto base = asFoldBase(expr.referencedGlobal());
        switch (cast<GlobalProgram>(base.program)->globalKind()) {
        case GlobalKind::Var:
            return Constant(ExpressionCategory::SharedReference);
        case GlobalKind::ConstVar:
        case GlobalKind::Let:
        case GlobalKind::OpenLet:
            return Constant(ExpressionCategory::ConstSharedReference);
        default:
            VERIFY_NOT_REACHED();
        }
    }
    case ExpressionKind::TemplateParameterReference:
        return Constant(ExpressionCategory::ConstSharedReference);
    case ExpressionKind::VariableReference:
        return Constant(ExpressionCategory::UniqueReference);
    case ExpressionKind::ParameterReference:
        // TODO: References to the return value are currently represented as parameter references but are not handled here.
        return cast<FunctionProgram>(program)->functionParameters[expr.id()].category();
    case ExpressionKind::ReferenceReference:
        return localReferences[expr.referenceIndex()].category;
    case ExpressionKind::MemberExpression:
        return categoryOf(program->getMemberReference(expr).base);
    case ExpressionKind::Call:
        return program->getCall(expr).resultCategory;
    case ExpressionKind::ImplicitCopy:
        return Constant(ExpressionCategory::Value);
    default:
        VERIFY(expr.isConstant());
        return Constant(ExpressionCategory::Value);
    }
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
    case ConstantKind::ExpressionCategoryLiteral:
        return builtins::expression_category_type;
    case ConstantKind::Computed:
        return program->getComputedConstant(value).type;
    case ConstantKind::RemoteComputed: {
        auto rExpr = program->getRemoteComputedConstant(value);
        auto base = asFoldBase(rExpr.base);
        return verifyType(fold(base, base.program->getComputedConstant(rExpr.computation).type));
    }
    case ConstantKind::MemberPointer: {
        MemberPointer pointer = program->getMemberPointer(value);
        std::array<Constant, 2> arguments { pointer.originType(), pointer.memberType() };
        return verifyType(makeParameterize(builtins::member_ptr_template.program(), arguments));
    }
    case ConstantKind::EnumValue:
        return verifyType((Constant)program->getEnumValue(value).enumType);
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
        return cast<FunctionProgram>(program)->functionParameters[expr.parameterIndex()].type();
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
    case ProgramKind::Struct:
    case ProgramKind::Enum:
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
        case ProgramKind::Struct:
        case ProgramKind::Enum:
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

    static ProgramHandle createScope(Context& context, ProgramKind progKind, BuiltinId id) {
        auto value = context.pushStaticScope(progKind, nameOf(id), {}, {});
        VERIFY(value.kind() == DeclarationValueKind::Program);
        VERIFY(value.id() == std::to_underlying(id));
        return value.program();
    }

    void addEnumValue(Word name, Constant expectedValue) {
        auto* enumProgram = cast<EnumProgram>(program);
        int_t valueIndex = enumProgram->values.size();
        enumProgram->values.emplace_back(SourceLocation(), name, std::nullopt);
        enumProgram->addDeclaration(name, DeclarationValue(DeclarationValueKind::EnumValue, valueIndex));

        VERIFY(!program->isDependent());
        Constant actualValue = program->addEnumValue(context, { (Type)programHandle, (uint32_t)valueIndex });
        VERIFY(actualValue.kind() != ConstantKind::EnumValue);
        VERIFY(actualValue == expectedValue);
    }

    BuiltinGenerator(Context& context, ProgramKind progKind, BuiltinId id)
        : Generator(context, createScope(context, progKind, id)) {
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
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::type_type };
    }
    {
        BuiltinGenerator g { context, ProgramKind::Enum, BuiltinId::bool_type };
        g.addEnumValue(parse::words["false"], builtins::false_constant);
        g.addEnumValue(parse::words["true"], builtins::true_constant);
    }
    {
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::error_type };
    }
    {
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::namespace_type };
    }
    {
        BuiltinGenerator g { context, ProgramKind::Enum, BuiltinId::expression_category_type };
        // Must match the order of the c++ enum
        g.addEnumValue(parse::words["value"], Constant(ExpressionCategory::Value));
        g.addEnumValue(parse::words["unique_ref"], Constant(ExpressionCategory::UniqueReference));
        g.addEnumValue(parse::words["const_unique_ref"], Constant(ExpressionCategory::ConstUniqueReference));
        g.addEnumValue(parse::words["shared_ref"], Constant(ExpressionCategory::SharedReference));
        g.addEnumValue(parse::words["const_shared_ref"], Constant(ExpressionCategory::ConstSharedReference));
    }

    // typeof(tempalte(T: type) => expr) = template_id{template(T: type) -> typeof(expr)}
    // cast{type}(template(T: type) -> type_expr) = template_id{template(T: type) -> type_expr}

    // template(sig: template_signature) struct template_id: { }
    // typof(template_id) = typeof(template(sig: template_signature) => template_id{sig})
    //                    = template_id{template(sig: template_signature) -> typeof(template_id{sig})}
    //                    = template_id{template(sig: template_signature) -> type}
    {
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::template_signature_type };
    }
    {
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::template_id_template };
        g.addExplicitParameter(parse::words["sig"], builtins::template_signature_type, {});
    }

    // template(sig: function_signature) struct function_id: { }
    // typeof(function_id) = typeof(template(sig: function_signature) => function_id{sig})
    //                     = template_id{template(sig: function_signature) -> typeof(function_id{sig})}
    //                     = template_id{template(sig: function_signature) -> type}
    {
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::function_signature_type };
    }
    {
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::function_id_template };
        g.addExplicitParameter(parse::words["sig"], builtins::function_signature_type, {});
    }

    // template(pointee_type: type) struct ptr: { }
    {
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::ptr_template };
        g.addExplicitParameter(parse::words["pointee_type"], builtins::type_type, {});
    }

    // template(parent_type: type, member_type: type) struct member_ptr: { }
    {
        BuiltinGenerator g { context, ProgramKind::Struct, BuiltinId::member_ptr_template };
        g.addExplicitParameter(parse::words["parent_type"], builtins::type_type, {});
        g.addExplicitParameter(parse::words["member_type"], builtins::type_type, {});
    }

    // cast{template_id}( template_function_id{template(T: type) fn(t: T) -> T)} )
    //   = template_id{ template(T: type) -> function_id{fn(t: T) -> T} }

    // template(T: type) fn(t: T) -> T = template(T: type) -> function_id{fn(t: T) -> T}

    // typeof( (arg = expr) ) = cast{type}( (arg = typeof(expr)) ) = tuple{cast{tuple_signature}( (arg = typeof(expr)) )}
}

}