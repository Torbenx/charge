#include <sema/Context.h>
#include <sema/Generator.h>
#include <sema/errors.h>

#include <ranges>

namespace sema {

Generator::StashedExpression Generator::stashTopExpression() {
    resolveLazyExpressions();
    return { std::move(currentExpression), (uint32_t)expressionStack.size() };
}

void Generator::unstashTopExpression(StashedExpression e) {
    VERIFY(currentExpression == INVALID_EXPRESSION);
    VERIFY(expressionStack.size() == e.expectedExpressionStackSize);
    currentExpression = std::move(e.expr);
}

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

void Generator::emitExpression(std::optional<TokenInfo*> token, OwnedExpression e) {
    VERIFY(currentExpression == INVALID_EXPRESSION);
    if (token.has_value())
        token->setData2((Expression)e);
    currentExpression = std::move(e);
    expressionStack.push_back({ .endOffset = (uint32_t)instructionScratch.size() });
}

void Generator::emitCall(std::optional<TokenInfo*> token, Constant callTarget, std::vector<Expression> arguments) {
    auto base = asFoldBase(callTarget);
    Type returnType = verifyType(base.program->kind() == ProgramKind::Struct ? callTarget : fold(callTarget, cast<FunctionProgram>(base.program)->returnType()));
    OwnedExpression callExpression = program->addCall({ Constant(ExpressionCategory::Value), callTarget, returnType, std::move(arguments) });
    SourceLocation location = token.has_value() ? token->location() : SourceLocation();
    instructionScratch.emplace_back(Opcode::Call, location, Instruction::Data { .callExpression = callExpression });
    emitExpression(token, std::move(callExpression));
}

void Generator::implicitCopy(std::optional<TokenInfo*> implicitActionToken) {
    std::array<Constant, 1> templateArgs { resultType(topExpression()) };
    auto callTarget = makeParameterize(builtins::copy_function.program(), templateArgs);
    emitCall(implicitActionToken, callTarget, { takeTopExpression() });
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
    auto parentImpl = asFoldBase(parentImplOf);

    auto implTarget = cast<ScopeProgram>(parentImpl.program)->getDeclaration(program->name());
    if (!implTarget.has_value()) {
        error<errors::ImplicitImplTargetNotFound>();
        return false;
    }
    if (implTarget.value().kind() != DeclarationValueKind::Program) {
        error<errors::ImplicitImplTargetNotAProgram>();
        return false;
    }
    ProgramHandle implOfProgHandle = context.translate(parentImpl.programHandle, implTarget.value().program());
    signatureCheck(context, implOfProgHandle);
    Program* implOfProg = context.program(implOfProgHandle);

    if (program->kind() != implOfProg->kind()) {
        error<errors::ImplicitImplTargetKindMismatch>();
        return false;
    }
    DeductionState state(context, implOfProgHandle);
    VERIFY(implOfProg->inheritedParameterCount == parentImpl.arguments.size());
    for (int_t i = 0; i < (int_t)implOfProg->inheritedParameterCount; i++)
        state.explicitArgument(i, parentImpl.arguments[i]);

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
            if (implParameterIndex != (int_t)implOfProg->parameters.size()) {
                error<errors::ImplicitImplTemplateParameterCountMismatch>();
                return false;
            }
            break;
        }
        if (implParameterIndex == (int_t)implOfProg->parameters.size()) {
            error<errors::ImplicitImplTemplateParameterCountMismatch>();
            return false;
        }

        auto parameter = program->parameters[parameterIndex];
        auto implParameter = implOfProg->parameters[implParameterIndex];
        if (parameter.name != implParameter.name)
            error<errors::ImplicitImplTemplateParameterNameMismatch>();
        bool match = staticMatch(state, implParameter.type, (Constant)parameter.type);
        if (!match)
            error<errors::ImplicitImplTemplateParameterTypeMismatch>();
        state.explicitArgument(implParameterIndex, Constant(ConstantKind::CopyOfParameter, parameterIndex));
        // TODO: What about the initializer?
    }

    lazyParameterizeState = std::move(state);
    emitExpression({}, Expression(ExpressionKind::LazyParameterize, 0));
    return true;
}

void Generator::addParameterizeArguments(DeductionState& state, int_t firstParameterIndex) {
    VERIFY(firstParameterIndex >= state.program->inheritedParameterCount);
    VERIFY(tok->kind() == Token::Parameterize);
    auto argumentNames = context.parseOutput.argumentNames(tok->data1<parse::CallArgumentsHandle>());
    advance();

    int_t parameterCount = state.arguments.size();
    int_t pIndex = firstParameterIndex;
    int_t aIndex = 0;
    for (; tok->kind() == Token::CallArgument; aIndex++, pIndex++) {
        TokenInfo* callArgumentToken = tok;
        advance();

        // Find next explicit parameter
        while (pIndex < parameterCount && state.program->parameters[pIndex].implicit())
            pIndex += 1;
        if (pIndex == parameterCount)
            error<errors::ParameterizeWithTooManyArguments>();
        const auto& parameter = state.program->parameters[pIndex];
        if (argumentNames[aIndex].empty() || parameter.name == argumentNames[aIndex]) {
            visitExpression();
            initialize(callArgumentToken, state, Constant(ExpressionCategory::Value), parameter.type);
            state.explicitArgument(pIndex, expressionToConstant(callArgumentToken));
        } else
            error<errors::ParameterizeArgumentNameMismatch>();
    }
    VERIFY(aIndex == (int_t)argumentNames.size());
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();
}

void Generator::generateParameterizeExpr() {
    Expression baseResult = topExpression();
    if (baseResult.isConstant()) {
        Constant baseValue = baseResult.constant();
        takeTopExpression();

        auto generate = [this](DeductionState state) {
            addParameterizeArguments(state, state.program->inheritedParameterCount);
            lazyParameterizeState = std::move(state);
            emitExpression({}, Expression(ExpressionKind::LazyParameterize, 0));
        };

        if (baseValue.kind() == ConstantKind::Program) {
            Program* baseProg = context.program(baseValue.program());
            if (!baseProg->isTemplate())
                error<errors::ParameterizeBaseIsNotATemplate>();
            VERIFY(baseProg->inheritedParameterCount == 0);
            DeductionState state(context, baseValue.program());
            generate(std::move(state));
            return;
        }
        if (baseValue.kind() == ConstantKind::Parameterize) {
            auto basePara = program->getParameterize(baseValue);
            Program* baseProg = context.program(basePara.base);
            if (!baseProg->isTemplate())
                error<errors::ParameterizeBaseIsNotATemplate>();
            if (basePara.arguments.size() > baseProg->inheritedParameterCount)
                error<errors::ParameterizeBaseIsAlreadyParameterized>();
            VERIFY(basePara.arguments.size() == baseProg->inheritedParameterCount);
            DeductionState state(context, basePara.base);
            for (int_t i = 0; i < (int_t)baseProg->inheritedParameterCount; i++)
                state.explicitArgument(i, basePara.arguments[i]);
            generate(std::move(state));
            return;
        }
    }
    error<errors::ParameterizeBaseNotSupported>();
}

Generator::CallTarget Generator::resolveCallTarget(std::span<const Word> argumentNames) {
    Expression baseResult = INVALID_EXPRESSION;
    if (isTopExpressionLazyParameterize()) {
        DeductionState state = takeLazyParameterize();
        if (state.program->kind() == ProgramKind::Global) {
            if (!state.isComplete())
                error<errors::CallTargetIsIncompleteGlobal>();
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
            DeductionState state(context, baseValue.program(), baseProg->parameters.size());
            state.copyParameters(baseProg->inheritedParameterCount);
            takeTopExpression();
            return { std::move(state) };
        } else if (baseValue.kind() == ConstantKind::Parameterize) {
            auto param = program->getParameterize(baseValue);
            Program* baseProg = context.program(param.base);
            // VERIFY(cast<CallableProgram>(baseProg)->runtimeParameters.size() == argumentNames.size());
            VERIFY(param.arguments.size() <= baseProg->parameters.size());
            DeductionState state(context, param.base);
            for (int_t i = 0; i < (int_t)param.arguments.size(); i++)
                state.explicitArgument(i, param.arguments[i]);
            takeTopExpression();
            return { std::move(state) };
        }
    }
    error<errors::CallTargetNotSupported>();
    VERIFY_NOT_REACHED();
}

void Generator::generateCallExpr(CallTarget target) {
    VERIFY(tok->kind() == Token::CallExpr);
    TokenInfo* callToken = tok;
    advance();

    auto& state = target.state;
    if (state.program->kind() == ProgramKind::Function || state.program->kind() == ProgramKind::Struct) {

        auto arguments = visit<callParameters>(state.program, [this, &state](auto parameters) { return generateCallArguments(state, false, parameters); });

        if (!state.isComplete())
            error<errors::CallTargetTemplateArgumentDeductionIncomplete>();
        Constant callTarget = makeParameterize(state.programHandle, state.arguments);
        emitCall(callToken, callTarget, std::move(arguments));
        return;
    }
    VERIFY_NOT_REACHED();
}

template<std::ranges::random_access_range R>
std::vector<Expression> Generator::generateCallArguments(DeductionState& state, bool withSelfArgument, R parameters) {
    int_t parameterCount = std::ssize(parameters);
    std::vector<Expression> arguments;
    arguments.resize(parameterCount, INVALID_EXPRESSION);

    int_t argumentIndex = 0;
    if (withSelfArgument) {
        VERIFY(argumentIndex < parameterCount);
        CallParameter parameter = parameters[argumentIndex];
        initialize(std::nullopt, state, parameter.expectedInitializerCategory, parameter.type);
        arguments[argumentIndex] = takeTopExpression().release();
        argumentIndex += 1;
    }
    while (tok->kind() == Token::CallArgument) {
        VERIFY(argumentIndex < parameterCount);
        TokenInfo* callArgumentToken = tok;
        advance();
        visitExpression();
        CallParameter parameter = parameters[argumentIndex];
        initialize(callArgumentToken, state, parameter.expectedInitializerCategory, parameter.type);
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

Type Generator::memberType(Type originType, std::span<const uint32_t> memberIndices) {
    Type type = originType;
    for (uint32_t memberIndex : memberIndices) {
        auto base = asFoldBase(type);
        const auto& members = cast<StructProgram>(base.program)->members;
        VERIFY(memberIndex < members.size());
        type = verifyType(fold(base, members[memberIndex].type()));
    }
    return type;
}

void Generator::internalLookupRecurse(InternalLookupState& state, ModuleHandle module, ScopeProgram* prog) {
    auto maybeResult = prog->getDeclaration(state.lookupName);
    if (maybeResult.has_value()) {
        state.setResult(context.translate(module, maybeResult.value()));
        return;
    }

    if (prog->kind() != ProgramKind::Struct)
        return;

    auto* structProg = cast<StructProgram>(prog);
    for (int_t memberIndex = 0; memberIndex < (int_t)structProg->members.size(); memberIndex++) {
        const auto& member = structProg->members[memberIndex];
        if (member.name() == state.lookupName) {
            state.setResult({ DeclarationValueKind::Member, (uint32_t)memberIndex });
            return;
        }

        if (!member.isHas())
            continue;
        auto memberProg = structProg->baseProgram(member.type());
        if (!memberProg.has_value())
            continue;
        state.memberIndices.push_back(memberIndex);
        internalLookupRecurse(state, context.moduleOf(memberProg.value()), cast<ScopeProgram>(context.program(memberProg.value())));
        state.memberIndices.pop_back();
    }
}

Generator::InternalLookupResult Generator::internalLookup(ProgramHandle typeProg, Word name) {
    InternalLookupState state(name);
    internalLookupRecurse(state, context.moduleOf(typeProg), cast<ScopeProgram>(context.program(typeProg)));
    return state.result;
}

Expression Generator::generateDeclarationLiteral(InternalLookupResult result, Type parent) {
    VERIFY(result.value.has_value());
    return generateDeclarationLiteral(result.value.value(), memberType(parent, result.memberIndices));
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
    Word name = tok->data1<Word>();
    if (name == parse::words["false"]) {
        emitExpression(tok, builtins::false_constant);
        return;
    }
    if (name == parse::words["true"]) {
        emitExpression(tok, builtins::true_constant);
        return;
    }
    if (name == parse::words["self"]) {
        emitExpression(tok, lookupSelfParameter());
        return;
    }
    if (name == parse::words["self_type"]) {
        emitExpression(tok, lookupSelfType());
        return;
    }
    for (auto lookupCtx : std::views::reverse(lookupStack)) {
        switch (lookupCtx.kind()) {
        case LookupContext::Kind::Namespace: {
            auto result = lookupCtx.getNamespace()->getDeclaration(name);
            if (result.has_value()) {
                emitExpression(tok, generateDeclarationLiteral(result.value(), std::nullopt));
                return;
            }
            continue;
        }
        case LookupContext::Kind::TemplateParameters: {
            Program* prog = lookupCtx.getTemplateParameters();
            for (int_t i = prog->inheritedParameterCount; i < (int_t)prog->parameters.size(); i++) {
                if (prog->parameters[i].name == name) {
                    emitExpression(tok, Expression::templateParameterReference(i));
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
                    emitExpression(tok, entry.data);
                    return;
                }
            }
            continue;
        }
        case LookupContext::Kind::ContainingType: {
            Type type = lookupCtx.getContainingType();
            auto typeProg = baseProgram(type);
            if (!typeProg.has_value())
                continue;
            auto result = internalLookup(typeProg.value(), name);
            if (result.value.has_value()) {
                if (result.value->kind() == DeclarationValueKind::Member)
                    // Member should be looked up with .member or ::member
                    error<errors::UnqualifiedLookupFoundMember>();
                emitExpression(tok, generateDeclarationLiteral(std::move(result), type));
                return;
            }
            auto base = asFoldBase(type);
            for (int_t i = base.program->inheritedParameterCount; i < (int_t)base.program->parameters.size(); i++) {
                if (base.program->parameters[i].name == name) {
                    emitExpression(tok, Expression::templateParameterReference(i));
                    return;
                }
            }
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }
    error<errors::UnqualifiedLookupFailed>();
}

void Generator::generateStaticAccessExpr() {
    Word name = tok->data1<Word>();
    auto maybeBaseValue = expressionToConstantNoNewComputedConstants();
    if (!maybeBaseValue.has_value())
        error<errors::StaticLookupBaseExpressionNotSupported>();

    Constant baseValue = maybeBaseValue.value();
    if (baseValue.kind() == ConstantKind::Namespace) {
        Namespace* ns = context.getNamespace(baseValue.nsHandle());
        emitExpression(tok, generateDeclarationLiteral(ns->getDeclaration(name).value(), std::nullopt));
        return;
    }
    if (baseValue.kind() == ConstantKind::Program || baseValue.kind() == ConstantKind::Parameterize) {
        FoldBase base = asFoldBase(baseValue);
        if (base.program->kind() == ProgramKind::Struct || base.program->kind() == ProgramKind::Enum) {
            auto result = internalLookup(base.programHandle, name);
            if (!result.value.has_value())
                error<errors::StaticLookupFailed>();
            if (result.value->kind() == DeclarationValueKind::Member) {
                result.extendMemberIndices();
                auto memberPointer = generateMemberPointer(verifyType(baseValue), result.memberIndices);
                emitExpression(tok, program->addMemberPointer(context, memberPointer));
            } else {
                emitExpression(tok, generateDeclarationLiteral(std::move(result), verifyType(baseValue)));
            }
            return;
        }
    }
    error<errors::StaticLookupBaseConstantNotSupported>();
    VERIFY_NOT_REACHED();
}

void Generator::generateMemberAccessExpr() {
    VERIFY(tok->kind() == Token::MemberAccessExpr);
    TokenInfo* memberAccessToken = tok;
    advance();

    Type baseType = resultType(topExpression());
    auto result = internalLookup(baseProgram(baseType).value(), memberAccessToken->data1<Word>());
    if (!result.value.has_value())
        error<errors::MemberLookupFailed>();
    if (result.value->kind() == DeclarationValueKind::Member) {
        result.extendMemberIndices();
        auto memberPointer = generateMemberPointer(baseType, result.memberIndices);
        auto memberExpr = program->addMemberExpression({ takeTopExpression(), program->addMemberPointer(context, memberPointer) });
        emitExpression(memberAccessToken, memberExpr);
        return;
    }

    auto selfExprStash = stashTopExpression();
    if (result.value->kind() != DeclarationValueKind::Program)
        error<errors::MemberLookupResultNotSupported>();
    auto progHandle = result.value->program();
    signatureCheck(context, progHandle);
    if (context.program(progHandle)->kind() != ProgramKind::Function)
        error<errors::MemberLookupResultNotSupported>();
    auto* fnProg = cast<FunctionProgram>(context.program(progHandle));

    DeductionState state(context, progHandle);
    auto inheritedArguments = asFoldBase(memberType(baseType, result.memberIndices)).arguments;
    VERIFY(inheritedArguments.size() == fnProg->inheritedParameterCount);
    for (int_t i = 0; i < (int_t)fnProg->inheritedParameterCount; i++)
        state.explicitArgument(i, inheritedArguments[i]);

    if (fnProg->functionParameters.size() == 0)
        error<errors::MemberFunctionCallTargetHasNoSelfParameter>();
    const auto& firstFnParameter = fnProg->functionParameters.front();
    if (firstFnParameter.name() != parse::words["self"])
        error<errors::MemberFunctionCallTargetHasNoSelfParameter>();
    bool selfTypeMatch = staticMatch(state, firstFnParameter.type(), baseType);
    if (!selfTypeMatch)
        error<errors::MemberFunctionCallSelfParameterTypeMismatch>();

    if (tok->kind() == Token::Parameterize) {
        int_t firstTemplateParameter = fnProg->inheritedParameterCount;
        if (fnProg->isTemplate() && fnProg->parameters[fnProg->inheritedParameterCount].name == parse::words["self_type"])
            firstTemplateParameter += 1;
        addParameterizeArguments(state, firstTemplateParameter);
    }

    if (tok->kind() != Token::CallExpr)
        error<errors::MemberLookupFunctionResultNotImmediatelyCalled>();
    TokenInfo* callToken = tok;
    auto callArgumentNames = context.parseOutput.argumentNames(tok->data1<parse::CallArgumentsHandle>());
    advance();
    unstashTopExpression(std::move(selfExprStash));
    std::vector<Expression> callArguments = generateCallArguments(state, true, callParameters<FunctionProgram>::get(fnProg));

    if (!state.isComplete())
        error<errors::MemberFunctionCallTargetTemplateArgumentDeductionIncomplete>();
    Constant callTarget = makeParameterize(state.programHandle, state.arguments);
    emitCall(callToken, callTarget, std::move(callArguments));
}

Expression Generator::lookupSelfParameter() {
    for (auto lookupCtx : std::views::reverse(lookupStack)) {
        if (lookupCtx.kind() != LookupContext::Kind::Local)
            continue;

        auto* g = lookupCtx.getLocal();
        if (g->localLookupEntries.empty())
            continue;
        auto firstEntry = g->localLookupEntries.front();
        if (firstEntry.name == parse::words["self"]) {
            VERIFY(firstEntry.data == Expression::parameterReference(0));
            return Expression::parameterReference(0);
        }
    }
    error<errors::SelfParameterLookupFailed>();
    VERIFY_NOT_REACHED();
}

Type Generator::lookupSelfType() {
    for (auto lookupCtx : std::views::reverse(lookupStack)) {
        switch (lookupCtx.kind()) {
        case LookupContext::Kind::TemplateParameters: {
            auto* prog = lookupCtx.getTemplateParameters();
            if (!prog->isTemplate())
                continue;

            auto firstNonInheritedParameter = prog->parameters[prog->inheritedParameterCount];
            if (firstNonInheritedParameter.name == parse::words["self_type"])
                return verifyType(Expression::templateParameterReference(prog->inheritedParameterCount).copyTemplateParameter());
            continue;
        }
        case LookupContext::Kind::ContainingType:
            return lookupCtx.getContainingType();
        default:
            continue;
        }
    }
    error<errors::SelfTypeTemplateParameterLookupFailed>();
    VERIFY_NOT_REACHED();
}

std::optional<Constant> Generator::copyAsConstant(Expression expr) {
    switch (expr.kind()) {
    case ExpressionKind::TemplateParameterReference:
        return expr.copyTemplateParameter();
    case ExpressionKind::GlobalReference$Program:
    case ExpressionKind::GlobalReference$Parameterize: {
        FoldBase base = asFoldBase(expr.referencedGlobal());
        VERIFY(base.program->kind() == ProgramKind::Global);
        auto* globalProg = cast<GlobalProgram>(base.program);
        if (globalProg->globalKind() == GlobalKind::Let) {
            return fold(base, cast<GlobalProgram>(base.program)->initializer());
        } else if (globalProg->globalKind() == GlobalKind::OpenLet) {
            return makeCopyOfOpenGlobal(base.value);
        } else {
            return std::nullopt;
        }
    }
    default:
        return std::nullopt;
    }
}

void Generator::toValueExpression(std::optional<TokenInfo*> implicitActionToken) {
    auto inCategory = categoryOf(topExpression());
    VERIFY(inCategory.kind() == ConstantKind::ExpressionCategoryLiteral); // TODO: Make this generic
    if (inCategory == Constant(ExpressionCategory::Value))
        return;

    if (auto constantCopy = copyAsConstant(topExpression()); constantCopy.has_value()) {
        takeTopExpression();
        emitExpression(implicitActionToken, constantCopy.value());
    } else {
        implicitCopy(implicitActionToken);
    }
}

Constant Generator::expressionToConstant(std::optional<TokenInfo*> implicitActionToken) {
    VERIFY(!expressionStack.empty());
    toValueExpression(implicitActionToken);
    return valueExpressionToConstant();
}

Constant Generator::valueExpressionToConstant() {
    if (topExpression().isConstant())
        return takeTopExpression().constant();
    VERIFY(categoryOf(topExpression()) == Constant(ExpressionCategory::Value));

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

std::optional<Constant> Generator::expressionToConstantNoNewComputedConstants() {
    if (topExpression().isConstant())
        return takeTopExpression().constant();

    if (auto constantCopy = copyAsConstant(topExpression()); constantCopy.has_value()) {
        takeTopExpression();
        return constantCopy.value();
    }
    return std::nullopt;
}

void Generator::contextualToType(std::optional<TokenInfo*> implicitActionToken) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, context.thisModule(), programHandle, 0);
    initialize(implicitActionToken, state, Constant(ExpressionCategory::Value), builtins::type_type);
}

void Generator::contextualToBool(std::optional<TokenInfo*> implicitActionToken) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, context.thisModule(), programHandle, 0);
    initialize(implicitActionToken, state, Constant(ExpressionCategory::Value), builtins::bool_type);
}

void Generator::contextualToExpressionCategory(std::optional<TokenInfo*> implicitActionToken) {
    // We can get away with this state because parameter-side value is so simple
    DeductionState state(program, context.thisModule(), programHandle, 0);
    initialize(implicitActionToken, state, Constant(ExpressionCategory::Value), builtins::expression_category_type);
}

void Generator::initialize(std::optional<TokenInfo*> implicitActionToken, DeductionState& state, ExternConstant expectedCategoryConstant, ExternConstant expectedType) {
    auto expr = topExpression();
    bool sameType = staticMatch(state, expectedType, resultType(expr));
    if (!sameType)
        error<errors::InitializeTypeMismatch>();
    auto inputCategoryConstant = categoryOf(expr);
    if (expectedCategoryConstant.kind() != ConstantKind::ExpressionCategoryLiteral) {
        bool match = staticMatch(state, expectedCategoryConstant, inputCategoryConstant);
        VERIFY(match); // The only way to get a mismatch is when both sides are literals, which is handled below.
        return;
    }

    VERIFY(inputCategoryConstant.kind() == ConstantKind::ExpressionCategoryLiteral); // TODO: Make this generic
    auto inputCategory = inputCategoryConstant.expressionCategory();
    auto expectedCategory = Constant(expectedCategoryConstant).expressionCategory();
    if (expectedCategory == ExpressionCategory::Value) {
        toValueExpression(implicitActionToken);
    } else {
        if (inputCategory == ExpressionCategory::Value)
            // initializing a reference with a value requires making temporary
            error<errors::InitializeOfReferenceWithValue>();

        if (expectedCategory != inputCategory) {
            // only down casts allowed
            if (expectedCategory == ExpressionCategory::SharedReference || expectedCategory == ExpressionCategory::ConstUniqueReference) {
                if (inputCategory != ExpressionCategory::UniqueReference)
                    error<errors::InitializeOfReferenceIsNotReferenceDowncast>();
            } else {
                if (expectedCategory != ExpressionCategory::ConstSharedReference)
                    error<errors::InitializeOfReferenceIsNotReferenceDowncast>();
            }
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
        return FoldBase { baseProg, context.moduleOf(base.program()), base.program(), base, {} };
    } else if (base.kind() == ConstantKind::Parameterize) {
        auto param = program->getParameterize(base);
        Program* baseProg = context.program(param.base);
        if (baseProg->parameters.size() != param.arguments.size())
            return std::nullopt;
        return FoldBase { context.program(param.base), context.moduleOf(param.base), param.base, base, param.arguments };
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
            state.equalities.add(context, programHandle, state.programHandle, { pValue, aValue });
        }
        return true;
    }

    if (pValue.kind() == ConstantKind::Computed || pValue.kind() == ConstantKind::RemoteComputed
        || aValue.kind() == ConstantKind::Computed || aValue.kind() == ConstantKind::RemoteComputed
        || pValue.kind() == ConstantKind::CopyOfOpenGlobal$Program || pValue.kind() == ConstantKind::CopyOfOpenGlobal$Parameterize
        || aValue.kind() == ConstantKind::CopyOfOpenGlobal$Program || aValue.kind() == ConstantKind::CopyOfOpenGlobal$Parameterize
        || pValue.kind() == ConstantKind::CopyOfParameterToReferenceCategory
        || aValue.kind() == ConstantKind::CopyOfParameterToReferenceCategory
        || aValue.kind() == ConstantKind::CopyOfParameter) {
        // TODO: check that the parameter-side value does not contain any non-explicit arguments
        state.equalities.add(context, programHandle, state.programHandle, { pValue, aValue });
        return true;
    }

    auto comparePrograms = [this, &state](ProgramHandle pProg, ProgramHandle aProg) {
        return context.translate(state.module, pProg) == aProg;
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
        return context.translate(state.module, Constant(pValue).nsHandle()) == aValue.nsHandle();
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
    auto foldProgram = [this, &base](ProgramHandle handle) {
        return context.translate(base.module, handle);
    };
    if (v == builtins::self_constant)
        v = base.program->selfConstant();
    switch (v.kind()) {
    case ConstantKind::Program:
        return (Constant)foldProgram(Constant(v).program());
    case ConstantKind::CopyOfParameter:
        return base.arguments[v.id()];
    case ConstantKind::CopyOfParameterToReferenceCategory:
        return genericToReferenceCategory(base.arguments[v.id()]);
    case ConstantKind::CopyOfOpenGlobal$Program:
    case ConstantKind::CopyOfOpenGlobal$Parameterize:
        return makeCopyOfOpenGlobal(fold(base, Constant(v).copiedGlobal()));
    case ConstantKind::Namespace:
        return (Constant)context.translate(base.module, Constant(v).nsHandle());
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
    VERIFY(program->status() == ProgramStatus::Unchecked); // TODO: This should be an error

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
            scope = context.translate(scope.program(), scopeProg->parent());
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

Constant Generator::genericToReferenceCategory(Constant genericCategory) {
    if (genericCategory.kind() == ConstantKind::ExpressionCategoryLiteral) {
        if (genericCategory.expressionCategory() == ExpressionCategory::Value)
            return Constant(ExpressionCategory::UniqueReference);
        return genericCategory;
    }

    if (genericCategory.kind() == ConstantKind::CopyOfParameter)
        return Constant(ConstantKind::CopyOfParameterToReferenceCategory, genericCategory.id());

    if (genericCategory.kind() == ConstantKind::CopyOfParameterToReferenceCategory)
        return genericCategory;

    // TODO: Generate a computed constant that does the conversion
    VERIFY_NOT_REACHED();
}

Constant Generator::referenceCategory(VariableCategory variableCategory) {
    if (variableCategory.isGeneric())
        return genericToReferenceCategory(variableCategory.genericCategory());
    switch (variableCategory.kind()) {
    case VariableKind::Let:
        return Constant(ExpressionCategory::ConstUniqueReference);
    case VariableKind::Var:
        return Constant(ExpressionCategory::UniqueReference);
    case VariableKind::UniqueReference:
        return Constant(ExpressionCategory::UniqueReference);
    case VariableKind::SharedReference:
        return Constant(ExpressionCategory::SharedReference);
    case VariableKind::ConstUniqueReference:
        return Constant(ExpressionCategory::ConstUniqueReference);
    case VariableKind::ConstSharedReference:
        return Constant(ExpressionCategory::ConstSharedReference);
    default:
        VERIFY_NOT_REACHED();
    }
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
        return referenceCategory(cast<FunctionProgram>(program)->functionParameters[expr.id()].category());
    case ExpressionKind::ReferenceReference:
        return localReferences[expr.referenceIndex()].category;
    case ExpressionKind::MemberExpression:
        return categoryOf(program->getMemberReference(expr).base);
    case ExpressionKind::Call:
        return program->getCall(expr).resultCategory;
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
    case ConstantKind::CopyOfParameterToReferenceCategory:
        return builtins::expression_category_type;
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

}