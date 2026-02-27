#include <sema/Context.h>
#include <sema/Generator.h>
#include <sema/errors.h>

namespace sema {

Generator::Generator(Context& context, ProgramHandle handle)
    : Util(context, handle) { }

void Generator::setParseLocation(parse::TokenHandle parseLocation) {
    VERIFY(parseLocation != parse::TokenHandle());
    tok = context.tokenBuffer.tokenPtr(parseLocation);
}

void Generator::clearParseLocation() {
    tok = nullptr;
}

void Generator::advance() { tok += 1; }

Generator::LocalScope Generator::beginLocalScope(SourceLocation location) {
    localScopeDepth += 1;
    return {
        localScopeDepth,
        (uint32_t)localState.variableActiveMask.size(),
        (uint32_t)localState.referenceActiveMask.size()
    };
}

void Generator::endLocalScope(LocalScope scope, SourceLocation location) {
    VERIFY(scope.localScopeDepth == localScopeDepth);
    localScopeDepth -= 1;

    VERIFY(localState.variableActiveMask.size() == localVariables.size());
    while (localState.variableActiveMask.size() > scope.localVariableCount) {
        if (localState.variableActiveMask.back()) {
            auto ref = Expression::variableReference(localState.variableActiveMask.size() - 1);
            emitDeactivate(location, ref);
        }
        localState.variableActiveMask.pop_back();
        localVariables.pop_back();
    }

    VERIFY(localState.referenceActiveMask.size() == localReferences.size());
    while (localState.referenceActiveMask.size() > scope.localReferenceCount) {
        if (localState.referenceActiveMask.back()) {
            auto ref = Expression::referenceReference(localState.referenceActiveMask.size() - 1);
            emitDeactivate(location, ref);
        }
        localState.referenceActiveMask.pop_back();
        localReferences.pop_back();
    }

    VERIFY(currentExpression == INVALID_EXPRESSION);
}

void Generator::declareLocalVariable(VariableDeclaration decl) {
    VERIFY(localVariables.size() == localState.variableActiveMask.size());
    int_t index = localVariables.size();
    localVariables.push_back({ decl.type });
    localState.variableActiveMask.push_back(decl.hasInitializer);
    Expression refExpr = Expression::variableReference(index);
    localLookupEntries.push_back({ decl.name, refExpr });
    decl.declaringToken->setData2<parse::DataKind::Expression>(refExpr);

    if (decl.hasInitializer)
        emitInitialize(decl.declaringToken->location(), Expression::variableReference(index), takeTopExpression());
}

void Generator::visitDeclaration() {
    if (tok->kind() == Token::TemplateAttribute) {
        visitTemplateParameters();
    }
    if (tok->kind() == Token::StructDecl) {
        visitStructDeclaration();
    } else if (tok->kind() == Token::StructImplDecl) {
        visitStructImplDeclaration();
    } else if (tok->kind() == Token::GlobalDecl) {
        visitStaticVariableDeclaration();
    } else if (tok->kind() == Token::GlobalImplDecl) {
        visitStaticVariableImplDeclaration();
    } else if (tok->kind() == Token::FunctionDecl) {
        visitFunctionDeclaration();
    } else if (tok->kind() == Token::FunctionImplDecl) {
        visitFunctionImplDeclaration();
    } else if (tok->kind() == Token::EnumDecl) {
        visitEnumDeclaration();
    } else if (tok->kind() == Token::EnumImplDecl) {
        visitEnumImplDeclaration();
    } else {
        VERIFY_NOT_REACHED();
    }
}

void Generator::visitTemplateParameters() {
    VERIFY(program->parameters.size() == program->inheritedParameterCount);
    lookupStack.push_back(LookupContext::forTemplateParameters(program));
    advance();
    while (tok->kind() != Token::EmptyNode) {
        VERIFY(tok->kind() == Token::LetValueDecl);
        Word name = tok->data1<parse::DataKind::Word>();
        TokenInfo* declaringToken = tok;
        advance();

        Type type = Type(INVALID_CONSTANT);
        std::optional<Constant> defaultValue;
        if (name == parse::words["self_type"]) {
            // TODO: Allow default argument
            if (tok->kind() == Token::VariableType)
                error<errors::SelfTypeTemplateParameterWithExplicitType>();
            if (tok->kind() == Token::AssignStmt)
                error<errors::SelfTypeTemplateParameterWithDefaultArgument>();
            VERIFY(tok->kind() == Token::ExpressionStmt);
            advance();
            type = builtins::type_type;
        } else {
            auto info = visitVariableTypeAndInitializer(ImplicitParameterMode::AddToProgram, false);
            VERIFY(info.category.kind() == VariableKind::Let);
            type = info.type;
            if (info.hasInitializer)
                defaultValue = valueExpressionToConstant();
        }
        Expression refExpr = addExplicitParameter(declaringToken->location(), name, type, defaultValue);
        declaringToken->setData2<parse::DataKind::Expression>(refExpr);
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();
}

Generator::VariableDeclaration Generator::visitVariableDeclaration(ImplicitParameterMode parameterMode) {
    VERIFY(tok->kind() == Token::LetValueDecl || tok->kind() == Token::VarValueDecl);
    bool isVar = tok->kind() == Token::VarValueDecl;
    Word name = tok->data1<parse::DataKind::Word>();
    TokenInfo* declToken = tok;
    advance();

    auto info = visitVariableTypeAndInitializer(parameterMode, isVar);
    return { info, declToken, name };
}

VariableCategory Generator::visitVariableTypeToken(bool isVar) {
    VERIFY(tok->kind() == Token::VariableType);

    VariableKind kind = tok->data1<parse::DataKind::VariableKind>();
    advance();
    if (isVar) {
        VERIFY(kind == VariableKind::Let); // Dummy value
        return VariableKind::Var;
    }

    if (kind != VariableKind::Generic)
        return kind;

    VERIFY(tok->kind() == Token::VariableGenericCategory);
    TokenInfo* genericCategoryToken = tok;
    advance();
    visitExpression();
    contextualToExpressionCategory(genericCategoryToken);
    return VariableCategory(expressionToConstant(genericCategoryToken));
}

Generator::VariableTypeAndInitializer Generator::visitVariableTypeAndInitializer(ImplicitParameterMode parameterMode, bool isVar) {
    std::optional<Type> variableType;
    VERIFY(parameterTypes.size() == program->parameters.size()); // TODO: visitVariableExpressionCategory() may have already added parameters

    VariableCategory category(isVar ? VariableKind::Var : VariableKind::Let);
    if (tok->kind() == Token::VariableType) {
        TokenInfo* typeToken = tok;
        category = visitVariableTypeToken(isVar);

        if (tok->kind() != Token::AssignStmt && tok->kind() != Token::ExpressionStmt) {
            // parse type
            wildcardMeaning = WildcardMeaning::ImplicitTemplate;
            visitExpression();
            contextualToType(typeToken);
            variableType = verifyType(expressionToConstant(typeToken));
            wildcardMeaning = WildcardMeaning::Error;
        }
    }

    if (!variableType.has_value())
        variableType = verifyType(newImplicitParameter(builtins::type_type).copyTemplateParameter());
    Type type = variableType.value();

    if (tok->kind() == Token::ExpressionStmt) {
        advance();

        bool createdImplicitParameters = parameterTypes.size() != program->parameters.size();
        if (createdImplicitParameters) {
            if (parameterMode == ImplicitParameterMode::DeduceLocally) {
                VERIFY_NOT_REACHED(); // New parameter cannot be deduced. TODO: This should be an error.
            } else {
                // add implicit parameters to program
                while (program->parameters.size() < parameterTypes.size()) {
                    int_t parameterIndex = program->parameters.size();
                    program->parameters.push_back({ SourceLocation(), Word(), parameterTypes[parameterIndex], std::nullopt });
                }
            }
        }
        return { type, category, false };
    }

    VERIFY(tok->kind() == Token::AssignStmt);
    TokenInfo* assignToken = tok;
    advance();
    visitExpression();
    VERIFY(tok->kind() == Token::ExpressionStmt);
    advance();

    DeductionState state(context, programHandle, parameterTypes.size());
    state.copyParameters(program->parameters.size());
    initialize(assignToken, state, category.initializerCategory(), type);
    if (!state.isComplete())
        VERIFY_NOT_REACHED(); // TODO: This should be an error.
    // TODO: Verify that no computed constant in 'type' contains newly created implicit parameters

    if (parameterMode == ImplicitParameterMode::AddToProgram) {
        // add implicit parameters to program
        while (program->parameters.size() < parameterTypes.size()) {
            int_t parameterIndex = program->parameters.size();
            program->parameters.push_back({ SourceLocation(), Word(), parameterTypes[parameterIndex], state.arguments[parameterIndex] });
        }
    } else {
        type = verifyType(fold(state.toFoldBase(builtins::self_constant), type));
        // remove introduced parameters
        parameterTypes.erase(parameterTypes.begin() + program->parameters.size(), parameterTypes.end());
    }

    return { type, category, true };
}

void Generator::visitStaticVariableImplDeclaration() {
    VERIFY(program->kind() == ProgramKind::Global);
    auto* globalProgram = cast<GlobalProgram>(program);

    VERIFY(tok->kind() == Token::GlobalImplDecl);
    advance();

    visitExpression();
    Expression implOfRef = takeTopExpression();
    Constant implOf = implOfRef.referencedGlobal();
    VERIFY(tok->kind() == Token::ExpressionStmt);
    advance();

    auto info = visitVariableTypeAndInitializer(ImplicitParameterMode::DeduceLocally, false);
    VERIFY(info.category.kind() == VariableKind::Let);
    program->setType(info.type);
    if (info.hasInitializer)
        // TODO: Constants are required to be the same when copied.
        //       For global objects this restriction is not necessary.
        globalProgram->setInitializer(valueExpressionToConstant());
    else
        // TODO: Open globals should not need an initializer
        error<errors::StaticVariableDeclarationWithoutInitializer>();

    checkStaticVariableImplDeclaration(implOf);
}

void Generator::visitStaticVariableDeclaration() {
    VERIFY(program->kind() == ProgramKind::Global);
    auto* globalProgram = cast<GlobalProgram>(program);

    VERIFY(tok->kind() == Token::GlobalDecl);
    advance();

    auto info = visitVariableTypeAndInitializer(ImplicitParameterMode::DeduceLocally, false);
    VERIFY(info.category.kind() == VariableKind::Let);
    program->setType(info.type);
    if (info.hasInitializer)
        // TODO: Constants are required to be the same when copied.
        //       For global objects this restriction is not necessary.
        globalProgram->setInitializer(valueExpressionToConstant());
    else
        // TODO: Open globals should not need an initializer
        error<errors::StaticVariableDeclarationWithoutInitializer>();

    if (auto state = resolveImplicitImplTarget(); state.has_value()) {
        checkStaticVariableImplDeclaration(makeParameterize(state.value()));
    } else {
        Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
        context.completeSignatureCheck(programHandle, false, selfConstant);
    }
}

void Generator::checkStaticVariableImplDeclaration(Constant implOf) {
    auto base = asFoldBase(implOf);
    VERIFY(base.program->kind() == ProgramKind::Global);
    auto* baseProg = cast<GlobalProgram>(base.program);
    auto* implProg = cast<GlobalProgram>(program);

    if (baseProg->globalKind() != GlobalKind::OpenLet)
        error<errors::GlobalImplTargetNotOpen>();

    auto state = DeductionState::fromFoldBase(base);
    if (!staticMatch(state, baseProg->type(), (Constant)implProg->type()))
        error<errors::GlobalImplTypeMismatch>();

    context.completeSignatureCheck(programHandle, true, implOf);
}

void Generator::visitFunctionImplDeclaration() {
    VERIFY(tok->kind() == Token::FunctionImplDecl);
    advance();
    auto state = processPostfixExpr();
    if (!state.has_value()) {
        auto implOf = expressionToConstantNoNewComputedConstants();
        if (implOf.has_value()) {
            if (auto base = tryAsFoldBase(implOf.value()); base.has_value())
                state = DeductionState::fromFoldBase(base.value());
            else if (auto stateOpt = tryBeginParameterize(implOf.value()); stateOpt.has_value())
                state = stateOpt;
        }
    }

    visitFunctionParametersAndBody();

    if (state.has_value())
        checkFunctionImplDeclaration(state.value());
    else
        error<errors::ExplicitImplExpressionNotSupported>();
}

void Generator::visitFunctionDeclaration() {
    VERIFY(tok->kind() == Token::FunctionDecl);
    advance();
    visitFunctionParametersAndBody();

    if (auto state = resolveImplicitImplTarget(); state.has_value()) {
        checkFunctionImplDeclaration(std::move(state.value()));
    } else {
        Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
        context.completeSignatureCheck(programHandle, false, selfConstant);
    }
}

void Generator::checkFunctionImplDeclaration(DeductionState state) {
    auto* implProgram = cast<FunctionProgram>(program);
    auto* baseProg = cast<FunctionProgram>(state.program);

    if (implProgram->functionParameters.size() != baseProg->functionParameters.size())
        error<errors::FunctionImplFunctionParameterCountMismatch>();
    VERIFY(implProgram->functionParameters.size() == baseProg->functionParameters.size());
    for (int_t index = 0; index < (int_t)implProgram->functionParameters.size(); index++) {
        const auto& baseParameter = baseProg->functionParameters[index];
        const auto& implParameter = implProgram->functionParameters[index];
        if (baseParameter.name() != implParameter.name())
            error<errors::FunctionImplFunctionParameterNameMismatch>();
        if (baseParameter.kind() != implParameter.kind())
            error<errors::FunctionImplFunctionParameterKindMismatch>();
        if (baseParameter.kind() == VariableKind::Generic) {
            bool categoriesMatch = staticMatch(state, baseParameter.category().genericCategory(), implParameter.category().genericCategory());
            if (!categoriesMatch)
                error<errors::FunctionImplFunctionParameterGenericCategoryMismatch>();
        }

        bool typesMatch = staticMatch(state, baseParameter.type(), implParameter.type());
        if (!typesMatch)
            error<errors::FunctionImplFunctionParameterTypeMismatch>();

        // TODO: What about the initializer?
    }

    bool match = staticMatch(state, baseProg->returnType(), (Constant)implProgram->returnType());
    if (!match)
        error<errors::FunctionImplReturnTypeMismatch>();

    VERIFY(state.isComplete());
    context.completeSignatureCheck(programHandle, true, makeParameterize(state.programHandle, state.arguments));
}

void Generator::visitFunctionParametersAndBody() {
    VERIFY(program->kind() == ProgramKind::Function);
    auto* fnProgram = cast<FunctionProgram>(program);

    lookupStack.push_back(LookupContext::forLocal(this));

    auto addFunctionParameter = [&](VariableDeclaration info) {
        if (info.hasInitializer)
            error<errors::FunctionParameterWithDefaultArgument>();
        VERIFY(fnProgram->functionParameters.size() == localState.parameterActiveMask.size());
        int_t parameterIndex = fnProgram->functionParameters.size();
        localState.parameterActiveMask.push_back(true);
        Expression refExpr = Expression::parameterReference(parameterIndex);
        localLookupEntries.push_back({ info.name, refExpr });
        info.declaringToken->setData2<parse::DataKind::Expression>(refExpr);
        fnProgram->functionParameters.push_back({ info.declaringToken->location(), info.name, info.type, info.category });
    };

    if (tok->kind() != Token::EmptyNode) {
        Word name = tok->data1<parse::DataKind::Word>();
        if (name == parse::words["self"]) {
            TokenInfo* declToken = tok;
            bool isVar = tok->kind() == Token::VarValueDecl;
            advance();
            VariableCategory category(isVar ? VariableKind::Var : VariableKind::Let);
            if (tok->kind() == Token::VariableType) {
                category = visitVariableTypeToken(isVar);

                if (tok->kind() != Token::AssignStmt && tok->kind() != Token::ExpressionStmt)
                    error<errors::SelfFunctionParameterWithExplicitType>();
            }

            // TODO: Allow default argument?
            if (tok->kind() == Token::AssignStmt)
                error<errors::SelfFunctionParameterWithDefaultArgument>();
            VERIFY(tok->kind() == Token::ExpressionStmt);
            advance();

            addFunctionParameter({ { lookupSelfType(), category, false }, declToken, name });
        }
    }

    while (tok->kind() != Token::EmptyNode) {
        addFunctionParameter(visitVariableDeclaration(ImplicitParameterMode::AddToProgram));
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();

    VERIFY(expressionStack.size() == 1);
    VERIFY(expressionStack.front().endOffset == 0);
    VERIFY(instructionScratch.empty());
    if (tok->kind() == Token::BodyExpr) {
        TokenInfo* arrowToken = tok;
        auto bodyScopeInst = emitBlockScope(arrowToken->location());
        auto body = beginLocalScope(arrowToken->location());
        advance();

        visitExpression();
        VERIFY(tok->kind() == Token::ExpressionStmt);
        SourceLocation endLoc = tok->location();
        advance();

        toValueExpression(arrowToken);
        program->setType(resultType(topExpression()));
        emitInitialize(arrowToken->location(), Expression::returnValueReference(fnProgram), takeTopExpression());

        endLocalScope(body, endLoc);
        emitScopeEnd(endLoc, bodyScopeInst);
        fnProgram->setBody(takeInstructions());
    } else {
        if (tok->kind() == Token::ReturnType) {
            TokenInfo* arrowToken = tok;
            advance();
            if (tok->kind() == Token::IdentifierExpr && tok->data1<parse::DataKind::Word>() == parse::words["return_type"]) {
                advance();
                VERIFY(tok->kind() == Token::FunctionBody);
                if (!program->isTemplate())
                    error<errors::OpenReturnTypeOnNonTemplateFunction>();
                program->setType(makeOpenReturnType(builtins::self_constant));
            } else {
                visitExpression();
                contextualToType(arrowToken);
                program->setType(verifyType(expressionToConstant(arrowToken)));
            }
        } else {
            // TODO: Implement return type deduction
            error<errors::FunctionWithoutExplicitReturnType>();
        }
        VERIFY(tok->kind() == Token::FunctionBody);
        auto bodyScopeInst = emitBlockScope(tok->location());
        auto body = beginLocalScope(tok->location());
        advance();

        visitStatement();
        SourceLocation endLoc; // TODO: Should be the ';'
        endLocalScope(body, endLoc);
        emitScopeEnd(endLoc, bodyScopeInst);
        fnProgram->setBody(takeInstructions());
    }
}

void Generator::visitStructImplDeclaration() {
    VERIFY(tok->kind() == Token::StructImplDecl);
    advance();
    visitExpression();
    auto implOf = expressionToConstantNoNewComputedConstants();

    visitStructMembers();

    if (implOf.has_value())
        checkStructImplDeclaration(implOf.value());
    else
        error<errors::ExplicitImplExpressionNotSupported>();
}

void Generator::visitStructDeclaration() {
    VERIFY(tok->kind() == Token::StructDecl);
    advance();
    visitStructMembers();

    if (auto state = resolveImplicitImplTarget(); state.has_value()) {
        checkStructImplDeclaration(makeParameterize(state.value()));
    } else {
        Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
        context.completeSignatureCheck(programHandle, false, selfConstant);
    }
}

void Generator::checkStructImplDeclaration(Constant implOf) {
    auto base = asFoldBase(implOf);
    VERIFY(base.program->kind() == ProgramKind::Struct);
    // TODO: Do checks

    context.completeSignatureCheck(programHandle, true, implOf);
}

void Generator::visitStructMembers() {
    VERIFY(program->kind() == ProgramKind::Struct);
    auto* typeProgram = cast<StructProgram>(program);

    auto savedTok = tok;
    for (int_t i = 0; i < (int_t)typeProgram->members.size(); i++) {
        auto& member = typeProgram->members[i];
        VERIFY(!member.isChecked());

        setParseLocation(member.parseLocation());
        VERIFY(tok->kind() == Token::MemberDecl || tok->kind() == Token::HasMemberDecl);
        if (member.isHas())
            tok->setData1<parse::DataKind::DeclIndex>(i);
        else
            tok->setData2<parse::DataKind::DeclIndex>(i);
        TokenInfo* memberToken = tok;
        advance();

        TokenInfo* implicitActionToken = nullptr;
        if (member.isHas()) {
            implicitActionToken = memberToken;
        } else {
            if (tok->kind() != Token::VariableType)
                error<errors::MemberDeclarationWithoutExplicitType>();
            VERIFY(tok->data1<parse::DataKind::VariableKind>() == VariableKind::Let);
            implicitActionToken = tok;
            advance();
        }
        visitExpression();
        contextualToType(implicitActionToken);
        member.setType(verifyType(expressionToConstant(implicitActionToken)));
    }

    tok = savedTok;
}

void Generator::visitEnumImplDeclaration() {
    VERIFY(tok->kind() == Token::EnumImplDecl);
    advance();
    visitExpression();
    auto implOf = expressionToConstantNoNewComputedConstants();

    visitEnumValues();

    if (implOf.has_value())
        checkEnumImplDeclaration(implOf.value());
    else
        error<errors::ExplicitImplExpressionNotSupported>();
}

void Generator::visitEnumDeclaration() {
    VERIFY(tok->kind() == Token::EnumDecl);
    advance();
    visitEnumValues();

    if (auto state = resolveImplicitImplTarget(); state.has_value()) {
        checkEnumImplDeclaration(makeParameterize(state.value()));
    } else {
        Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
        context.completeSignatureCheck(programHandle, false, selfConstant);
    }
}

void Generator::checkEnumImplDeclaration(Constant implOf) {
    auto base = asFoldBase(implOf);
    VERIFY(base.program->kind() == ProgramKind::Enum); // TODO: This must be handled as an error somewhere.
    // TODO: Do checks

    context.completeSignatureCheck(programHandle, true, implOf);
}

void Generator::visitEnumValues() {
    VERIFY(program->kind() == ProgramKind::Enum);
    auto* enumProgram = cast<EnumProgram>(program);

    auto savedTok = tok;
    for (int_t i = 0; i < (int_t)enumProgram->values.size(); i++) {
        auto& value = enumProgram->values[i];
        VERIFY(!value.isChecked());

        setParseLocation(value.parseLocation());
        VERIFY(tok->kind() == Token::ImplicitEnumValueDecl || tok->kind() == Token::ExplicitEnumValueDecl);
        tok->setData2<parse::DataKind::DeclIndex>(i);
        advance();

        // TODO: Actually set value once ints are supported
        value.setValue(std::nullopt);
    }

    tok = savedTok;
}

void Generator::visitStatement() {
    if (tok->kind() == Token::CompoundStmt) {
        auto blockScopeInst = emitBlockScope(tok->location());
        auto scope = beginLocalScope(tok->location());
        advance();
        while (tok->kind() != Token::EmptyNode) {
            visitStatement();
        }
        VERIFY(tok->kind() == Token::EmptyNode);
        endLocalScope(scope, tok->location());
        emitScopeEnd(tok->location(), blockScopeInst);
        advance();
    } else if (tok->kind() == Token::LetValueDecl || tok->kind() == Token::VarValueDecl) {
        declareLocalVariable(visitVariableDeclaration(ImplicitParameterMode::DeduceLocally));
    } else if (tok->kind() == Token::DestroyStmt) {
        SourceLocation location = tok->location();
        advance();
        visitExpression();
        if (topExpression().kind() == ExpressionKind::VariableReference || topExpression().kind() == ExpressionKind::ParameterReference) {
            auto expr = takeTopExpression();
            localState.setActive(expr, false);
            emitDeactivate(location, expr);
        } else
            error<errors::DestoryTargetNotALocalVariable>();
    } else if (tok->kind() == Token::DiscardStmt) {
        SourceLocation location = tok->location();
        advance();
        visitExpression();

        if (topExpression().kind() == ExpressionKind::ReferenceReference) {
            auto expr = takeTopExpression();
            localState.setActive(expr, false);
            emitDeactivate(location, expr);
        } else
            error<errors::DiscardTargetNotALocalReference>();
    } else {
        visitExpression();
        if (tok->kind() == Token::IfStmt) {
            TokenInfo* ifToken = tok;
            advance();
            contextualToBool(ifToken);

            auto lastBranchInst = emitBranch(ifToken->location(), takeTopExpression());

            auto ifScope = beginLocalScope(ifToken->location());
            visitStatement();
            endLocalScope(ifScope, SourceLocation());

            if (tok->kind() == Token::ElseStmt) {
                SourceLocation elseLoc = tok->location();
                lastBranchInst = emitBranch(elseLoc, builtins::true_constant, lastBranchInst);

                advance();

                auto elseScope = beginLocalScope(elseLoc);
                visitStatement();
                endLocalScope(elseScope, SourceLocation());
            }

            emitScopeEnd(SourceLocation(), lastBranchInst);
        } else {
            VERIFY(tok->kind() == Token::ExpressionStmt);
            emitDiscard(tok->location(), takeTopExpression());
            advance();
        }
    }
}

void Generator::visitExpression() {
    visitBinaryExpr();
}

void Generator::visitBinaryExpr() {
    visitUnaryExpr();
}

namespace {
    ProgramHandle getUnaryFunction(parse::TokenKind kind) {
        switch (kind) {
        case parse::TokenKind::LogicalNotExpr:
            return builtins::logical_not_function.program();
        default:
            VERIFY_NOT_REACHED();
        }
    }
}

void Generator::visitUnaryExpr() {
    if (parse::isUnaryExpr(tok->kind())) {
        TokenInfo* unaryToken = tok;
        advance();
        visitUnaryExpr();
        std::array<Constant, 1> templateArgs { resultType(topExpression()) };
        Constant target = makeParameterize(getUnaryFunction(unaryToken->kind()), templateArgs);
        emitCall(unaryToken, target, { takeTopExpression().release() });
    } else {
        visitPostfixExpr();
    }
}

void Generator::visitPostfixExpr() {
    auto deductionState = processPostfixExpr();
    if (deductionState.has_value())
        emitExpression({}, makeParameterize(deductionState.value()));
}

std::optional<DeductionState> Generator::processPostfixExpr() {
    // Visit primary expression
    if (tok->kind() == Token::IdentifierExpr) {
        generateIdentifierExpr();
        advance();
    } else if (tok->kind() == Token::ImplicitSelfReference) {
        emitExpression(tok, lookupSelfParameter());
        advance();
        generateMemberAccessExpr();
    } else {
        VERIFY_NOT_REACHED();
    }

    // Visit postfixes
    std::optional<DeductionState> deductionState;
    auto resolveDeductionState = [this, &deductionState]() {
        if (deductionState.has_value()) {
            emitExpression({}, makeParameterize(deductionState.value()));
            deductionState.reset();
        }
    };
    for (;;) {
        if (tok->kind() == Token::Parameterize) {
            resolveDeductionState();
            deductionState = generateParameterizeExpr();
        } else if (tok->kind() == Token::CallExpr) {
            if (!deductionState.has_value()) {
                deductionState = resolveCallTarget(context.tokenBuffer.argumentNames(tok->data1<parse::DataKind::CallArguments>()));
            }
            generateCallExpr(std::move(deductionState.value()));
            deductionState.reset();
        } else if (tok->kind() == Token::StaticAccessExpr) {
            resolveDeductionState();
            generateStaticAccessExpr();
            advance();
        } else if (tok->kind() == Token::MemberAccessExpr) {
            resolveDeductionState();
            generateMemberAccessExpr();
        } else {
            break;
        }
    }
    return deductionState;
}

}