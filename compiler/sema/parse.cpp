#include <sema/Context.h>
#include <sema/Generator.h>
#include <sema/errors.h>

namespace sema {

Generator::Generator(Context& context, ProgramHandle handle)
    : Util(context, handle) { }

void Generator::setParseLocation(parse::TokenHandle parseLocation) {
    tok = context.parseOutput.tokens.data() + parseLocation.id();
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
    localLookupEntries.push_back({ decl.name, Expression::variableReference(index) });

    if (decl.hasInitializer)
        emitInitialize(decl.location, Expression::variableReference(index), takeTopExpression());
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
        Program::Parameter explicitParameter = visitTemplateParameter();
        VERIFY(!explicitParameter.name.empty());

        program->parameters.push_back(explicitParameter);
        parameterTypes.push_back(verifyType((Constant)explicitParameter.type));
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();
}

VariableCategory Generator::visitVariableCategory() {
    Token tokKind = tok->kind();
    advance();
    switch (tokKind) {
    case Token::LetValueDecl:
        return VariableKind::Let;
    case Token::VarValueDecl:
        return VariableKind::Var;
    case Token::UniqueReferenceDecl:
        return VariableKind::UniqueReference;
    case Token::SharedReferenceDecl:
        return VariableKind::SharedReference;
    case Token::ConstUniqueReferenceDecl:
        return VariableKind::ConstUniqueReference;
    case Token::ConstSharedReferenceDecl:
        return VariableKind::ConstSharedReference;
    case Token::GenericCategoryVariableDecl:
        visitExpression();
        contextualToExpressionCategory(SourceLocation());
        return VariableCategory(expressionToConstant());
    default:
        VERIFY_NOT_REACHED();
    }
}

Generator::VariableDeclaration Generator::visitVariableDeclaration(bool programParameters) {
    Word name = tok->data1<Word>();
    SourceLocation nameLoc = tok->location();

    auto category = visitVariableCategory();

    auto info = visitVariableTypeAndInitializer(category.initializerCategory(), programParameters);
    return { nameLoc, name, info.type, category, info.hasInitializer };
}

Generator::VariableTypeAndInitializer Generator::visitVariableTypeAndInitializer(Constant expectedCategory, bool programParameters) {
    std::optional<Type> variableType;
    VERIFY(parameterTypes.size() == program->parameters.size()); // TODO: visitVariableExpressionCategory() may have already added parameters
    if (tok->kind() != Token::AssignStmt) {
        // parse type
        wildcardMeaning = WildcardMeaning::ImplicitTemplate;
        SourceLocation conversionLocation = tok->location(); // TODO: Should be the ':', but there is currently no token for that
        visitExpression();
        contextualToType(conversionLocation);
        variableType = verifyType(expressionToConstant());
        wildcardMeaning = WildcardMeaning::Error;
    } else {
        variableType = verifyType(newImplicitParameter(builtins::type_type).copyTemplateParameter());
    }
    Type type = variableType.value();

    VERIFY(tok->kind() == Token::AssignStmt);
    SourceLocation assignLocation = tok->location();
    advance();

    bool hasInitializer = false;
    if (tok->kind() != Token::ExpressionStmt) {
        if (parameterTypes.size() != program->parameters.size() && programParameters) {
            // 'type' contains implicitly created parameters.
            // Converting the initializer to 'type' can never happend without deducing them.
            VERIFY_NOT_REACHED();
        }
        visitExpression();

        DeductionState state(context, programHandle, parameterTypes.size());
        state.copyParameters(program->parameters.size());
        initialize(assignLocation, state, expectedCategory, type);
        VERIFY(state.isComplete());
        // TODO: Verify that no computed constant in 'type' contains newly created implicit parameters
        type = verifyType(fold(state.toFoldBase(builtins::self_constant), type));

        hasInitializer = true;
    }
    VERIFY(tok->kind() == Token::ExpressionStmt);
    advance();

    if (programParameters) {
        // add implicit parameters to program
        while (program->parameters.size() < parameterTypes.size())
            program->parameters.push_back({ Word(), parameterTypes[program->parameters.size()], std::nullopt });
    } else {
        // remove introduced parameters
        parameterTypes.erase(parameterTypes.begin() + program->parameters.size(), parameterTypes.end());
    }

    return { type, hasInitializer };
}

Program::Parameter Generator::visitTemplateParameter() {
    if (tok->kind() != Token::LetValueDecl) {
        // report error
        VERIFY_NOT_REACHED();
    }
    Word name = tok->data1<Word>();
    advance();

    if (name == parse::words["self_type"]) {
        // TODO: Allow default argument
        if (tok->kind() != Token::AssignStmt)
            error<errors::SelfTypeTemplateParameterWithExplicitType>();
        VERIFY(tok->kind() == Token::AssignStmt);
        advance();
        if (tok->kind() != Token::ExpressionStmt)
            error<errors::SelfTypeTemplateParameterWithDefaultArgument>();
        VERIFY(tok->kind() == Token::ExpressionStmt);
        advance();
        return { name, builtins::type_type, std::nullopt };
    }

    auto info = visitVariableTypeAndInitializer(Constant(ExpressionCategory::Value), true);
    std::optional<Constant> initializer;
    if (info.hasInitializer)
        initializer = expressionToConstant();
    return { name, info.type, initializer };
}

void Generator::visitStaticVariableDeclaration() {
    VERIFY(program->kind() == ProgramKind::Global);
    auto* globalProgram = cast<GlobalProgram>(program);

    VERIFY(tok->kind() == Token::GlobalDecl);
    advance();

    auto info = visitVariableTypeAndInitializer(Constant(ExpressionCategory::Value), false);
    program->setType(info.type);
    if (info.hasInitializer)
        // TODO: Constants are required to be the same when copied.
        //       For global objects this restriction is not necessary.
        globalProgram->setInitializer(expressionToConstant());
    else
        // TODO: Open globals should not need an initializer
        error<errors::StaticVariableDeclarationWithoutInitializer>();

    Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
    program->completeSignatureCheck(false, selfConstant);
}

void Generator::visitFunctionImplDeclaration() {
    VERIFY(tok->kind() == Token::FunctionImplDecl);
    advance();
    visitExpression();
    Constant implOf = expressionToConstant();

    visitFunctionParametersAndBody();

    checkFunctionImplDeclaration(DeductionState::fromFoldBase(asFoldBase(implOf)));
}

void Generator::visitFunctionDeclaration() {
    VERIFY(tok->kind() == Token::FunctionDecl);
    advance();
    visitFunctionParametersAndBody();

    if (resolveImplicitImplTarget()) {
        auto parameterNamesRange = std::views::transform(cast<FunctionProgram>(program)->functionParameters, [](const FunctionProgram::Parameter& param) { return param.name(); });
        std::vector<Word> parameterNames { parameterNamesRange.begin(), parameterNamesRange.end() };
        auto [state] = resolveCallTarget(parameterNames);
        checkFunctionImplDeclaration(std::move(state));
    } else {
        Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
        program->completeSignatureCheck(false, selfConstant);
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
    program->completeSignatureCheck(true, makeParameterize(state.programHandle, state.arguments));
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
        localLookupEntries.push_back({ info.name, Expression::parameterReference(parameterIndex) });
        fnProgram->functionParameters.push_back({ info.location, info.name, info.type, info.category });
    };

    if (tok->kind() != Token::EmptyNode) {
        Word name = tok->data1<Word>();
        if (name == parse::words["self"]) {
            SourceLocation nameLoc = tok->location();
            VariableCategory category = visitVariableCategory();

            // TODO: Allow default argument?
            if (tok->kind() != Token::AssignStmt)
                error<errors::SelfFunctionParameterWithExplicitType>();
            VERIFY(tok->kind() == Token::AssignStmt);
            advance();
            if (tok->kind() != Token::ExpressionStmt)
                error<errors::SelfFunctionParameterWithDefaultArgument>();
            VERIFY(tok->kind() == Token::ExpressionStmt);
            advance();

            addFunctionParameter({ nameLoc, name, lookupSelfType(), category, false });
        }
    }

    while (tok->kind() != Token::EmptyNode) {
        addFunctionParameter(visitVariableDeclaration(true));
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();

    VERIFY(expressionStack.size() == 1);
    VERIFY(expressionStack.front().endOffset == 0);
    VERIFY(instructionScratch.empty());
    if (tok->kind() == Token::BodyExpr) {
        SourceLocation arrowLoc = tok->location();
        auto bodyScopeInst = emitBlockScope(arrowLoc);
        auto body = beginLocalScope(arrowLoc);
        advance();

        visitExpression();
        VERIFY(tok->kind() == Token::ExpressionStmt);
        SourceLocation endLoc = tok->location();
        advance();

        toValueExpression(arrowLoc);
        program->setType(resultType(topExpression()));
        emitInitialize(arrowLoc, Expression::returnValueReference(fnProgram), takeTopExpression());

        endLocalScope(body, endLoc);
        emitScopeEnd(endLoc, bodyScopeInst);
        fnProgram->setBody(takeInstructions());
    } else {
        if (tok->kind() == Token::ReturnType) {
            SourceLocation conversionLocation = tok->location();
            advance();
            visitExpression();
            contextualToType(conversionLocation);
            program->setType(verifyType(expressionToConstant()));
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
    Constant implOf = expressionToConstant();

    visitStructMembers();

    checkStructImplDeclaration(implOf);
}

void Generator::visitStructDeclaration() {
    VERIFY(tok->kind() == Token::StructDecl);
    advance();
    visitStructMembers();

    if (resolveImplicitImplTarget()) {
        checkStructImplDeclaration(expressionToConstant());
    } else {
        Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
        program->completeSignatureCheck(false, selfConstant);
    }
}

void Generator::checkStructImplDeclaration(Constant implOf) {
    auto base = asFoldBase(implOf);
    VERIFY(base.program->kind() == ProgramKind::Struct);
    // TODO: Do checks

    program->completeSignatureCheck(true, implOf);
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
        VERIFY(tok->data1<DeclarationValue>() == DeclarationValue(DeclarationValueKind::Member, i));
        advance();

        SourceLocation conversionLocation = tok->location(); // TODO: Should be the ':' for member declrations
        visitExpression();
        contextualToType(conversionLocation);
        member.setType(verifyType(expressionToConstant()));
    }

    tok = savedTok;
}

void Generator::visitEnumImplDeclaration() {
    VERIFY(tok->kind() == Token::EnumImplDecl);
    advance();
    visitExpression();
    Constant implOf = expressionToConstant();

    visitEnumValues();

    checkEnumImplDeclaration(implOf);
}

void Generator::visitEnumDeclaration() {
    VERIFY(tok->kind() == Token::EnumDecl);
    advance();
    visitEnumValues();

    if (resolveImplicitImplTarget()) {
        checkEnumImplDeclaration(expressionToConstant());
    } else {
        Constant selfConstant = makeParameterize(programHandle, copyParameters(program));
        program->completeSignatureCheck(false, selfConstant);
    }
}

void Generator::checkEnumImplDeclaration(Constant implOf) {
    auto base = asFoldBase(implOf);
    VERIFY(base.program->kind() == ProgramKind::Enum); // TODO: This must be handled as an error somewhere.
    // TODO: Do checks

    program->completeSignatureCheck(true, implOf);
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
        VERIFY(tok->data1<DeclarationValue>() == DeclarationValue(DeclarationValueKind::EnumValue, i));
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
    } else if (tok->kind() == Token::LetValueDecl || tok->kind() == Token::VarValueDecl
        || tok->kind() == Token::UniqueReferenceDecl || tok->kind() == Token::ConstUniqueReferenceDecl
        || tok->kind() == Token::SharedReferenceDecl || tok->kind() == Token::ConstSharedReferenceDecl) {
        declareLocalVariable(visitVariableDeclaration(false));
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
            SourceLocation ifLoc = tok->location();

            advance();
            contextualToBool(ifLoc);

            auto lastBranchInst = emitBranch(ifLoc, takeTopExpression());

            auto ifScope = beginLocalScope(ifLoc);
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

void Generator::visitUnaryExpr() {
    if (parse::isUnaryExpr(tok->kind())) {
        advance();
        visitUnaryExpr();
        // resolve and create call expression
    } else {
        visitPostfixExpr();
    }
}

void Generator::visitPostfixExpr() {
    visitPrimaryExpr();
    for (;;) {
        if (tok->kind() == Token::Parameterize) {
            generateParameterizeExpr();
        } else if (tok->kind() == Token::CallExpr) {
            CallTarget target = resolveCallTarget(context.parseOutput.argumentNames(tok->data1<parse::CallArgumentsHandle>()));
            SourceLocation callLoc = tok->location();
            advance();
            generateCallExpr(callLoc, std::move(target));
        } else if (tok->kind() == Token::StaticAccessExpr) {
            generateStaticAccessExpr();
            advance();
        } else if (tok->kind() == Token::MemberAccessExpr) {
            generateMemberAccessExpr();
        } else {
            break;
        }
    }
}

void Generator::visitPrimaryExpr() {
    if (tok->kind() == Token::IdentifierExpr) {
        generateIdentifierExpr();
        advance();
    } else if (tok->kind() == Token::MemberAccessExpr) {
        emitExpression(tok->location(), lookupSelfParameter()); // TODO: Location should be the '.' not the identifier
        generateMemberAccessExpr();
    } else {
        VERIFY_NOT_REACHED();
    }
}

}