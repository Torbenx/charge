#include <sema/Context.h>
#include <sema/Generator.h>

namespace sema {

namespace {
    ExpressionCategory expectedInitializerCategory(parse::TokenKind kind) {
        switch (kind) {
        case parse::TokenKind::LetValueDecl:
        case parse::TokenKind::VarValueDecl:
            return ExpressionCategory::Value;
        case parse::TokenKind::UniqueReferenceDecl:
            return ExpressionCategory::UniqueReference;
        case parse::TokenKind::SharedReferenceDecl:
            return ExpressionCategory::SharedReference;
        case parse::TokenKind::ConstUniqueReferenceDecl:
            return ExpressionCategory::ConstUniqueReference;
        case parse::TokenKind::ConstSharedReferenceDecl:
            return ExpressionCategory::ConstSharedReference;
        default:
            VERIFY_NOT_REACHED();
        }
    }
}

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

void Generator::declareLocalVariable(Word name, SourceLocation location, VariableDeclaration declaration) {
    VERIFY(localVariables.size() == localState.variableActiveMask.size());
    int_t index = localVariables.size();
    localVariables.push_back({ declaration.type });
    localState.variableActiveMask.push_back(declaration.hasInitializer);
    localLookupEntries.push_back({ name, Expression::variableReference(index) });

    if (declaration.hasInitializer)
        emitInitialize(location, Expression::variableReference(index), takeTopExpression());
}

void Generator::visitDeclaration() {
    if (tok->kind() == Token::TemplateAttribute) {
        visitTemplateParameters();
    }
    if (tok->kind() == Token::TypeDecl) {
        visitTypeDeclaration();
    } else if (tok->kind() == Token::TypeImplDecl) {
        visitTypeImplDeclaration();
    } else if (tok->kind() == Token::VarValueDecl || tok->kind() == Token::LetValueDecl) {
        visitStaticVariableDeclaration();
    } else if (tok->kind() == Token::FunctionDecl) {
        visitFunctionDeclaration();
    } else if (tok->kind() == Token::FunctionImplDecl) {
        visitFunctionImplDeclaration();
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

Generator::VariableDeclaration Generator::visitVariableDeclaration(ExpressionCategory expectedCategory, bool programParameters) {
    std::optional<Type> variableType;
    VERIFY(parameterTypes.size() == program->parameters.size());
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

        DeductionState state(program, programHandle, parameterTypes.size());
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
    Word name = Word::fromUint(tok->data());
    advance();

    auto info = visitVariableDeclaration(ExpressionCategory::Value, true);
    std::optional<Constant> initializer;
    if (info.hasInitializer)
        initializer = expressionToConstant();
    return { name, info.type, initializer };
}

void Generator::visitStaticVariableDeclaration() {
    VERIFY(program->kind() == ProgramKind::Global);
    auto* globalProgram = cast<GlobalProgram>(program);

    VERIFY(tok->kind() == Token::LetValueDecl || tok->kind() == Token::VarValueDecl);
    advance();

    auto info = visitVariableDeclaration(ExpressionCategory::Value, false);
    VERIFY(info.hasInitializer);
    program->setType(info.type);
    // TODO: Constants are required to be the same when copied.
    //       For global objects this restriction is not necessary.
    globalProgram->setInitializer(expressionToConstant());
}

void Generator::visitFunctionImplDeclaration() {
    VERIFY(tok->kind() == Token::FunctionImplDecl);
    advance();
    visitExpression();
    Constant implOf = expressionToConstant();
    VERIFY(implOf.kind() == ConstantKind::Parameterize);
    program->setImplConstant(implOf);
    auto base = asFoldBase(implOf);
    VERIFY(base.program->kind() == ProgramKind::Function);

    visitFunctionParametersAndBody();

    auto* implProgram = cast<FunctionProgram>(program);
    auto* baseProg = cast<FunctionProgram>(base.program);

    VERIFY(implProgram->runtimeParameters.size() == baseProg->runtimeParameters.size());
    auto state = DeductionState::fromFoldBase(base);
    for (int_t index = 0; index < (int_t)implProgram->runtimeParameters.size(); index++) {
        const auto& baseParameter = baseProg->runtimeParameters[index];
        const auto& implParameter = implProgram->runtimeParameters[index];
        VERIFY(baseParameter.name == implParameter.name);
        VERIFY(baseParameter.kind() == implParameter.kind());
        bool match = staticMatch(state, baseParameter.type(), implParameter.type());
        VERIFY(match);
    }

    bool match = staticMatch(state, baseProg->returnType(), (Constant)implProgram->returnType());
    VERIFY(match);
}

void Generator::visitFunctionDeclaration() {
    VERIFY(tok->kind() == Token::FunctionDecl);
    advance();
    visitFunctionParametersAndBody();
}

void Generator::visitFunctionParametersAndBody() {
    VERIFY(program->kind() == ProgramKind::Function);
    auto* fnProgram = cast<FunctionProgram>(program);

    lookupStack.push_back(LookupContext::forLocal(this));

    auto runtimeParameterKind = [](Token token) {
        switch (token) {
        case Token::LetValueDecl:
            return RuntimeParameterKind::LetVariable;
        case Token::VarValueDecl:
            return RuntimeParameterKind::VarVariable;
        case Token::UniqueReferenceDecl:
            return RuntimeParameterKind::UniqueReference;
        case Token::SharedReferenceDecl:
            return RuntimeParameterKind::SharedReference;
        case Token::ConstUniqueReferenceDecl:
            return RuntimeParameterKind::ConstUniqueReference;
        case Token::ConstSharedReferenceDecl:
            return RuntimeParameterKind::ConstSharedReference;
        default:
            VERIFY_NOT_REACHED();
        }
    };

    while (tok->kind() != Token::EmptyNode) {
        auto parameterKind = runtimeParameterKind(tok->kind());
        auto expectedCategory = expectedInitializerCategory(tok->kind());
        Word name = Word::fromUint(tok->data());
        auto nameLoc = tok->location();
        advance();
        auto info = visitVariableDeclaration(expectedCategory, true);
        VERIFY(!info.hasInitializer);

        VERIFY(fnProgram->runtimeParameters.size() == localState.parameterActiveMask.size());
        int_t parameterIndex = fnProgram->runtimeParameters.size();
        localState.parameterActiveMask.push_back(true);
        localLookupEntries.push_back({ name, Expression::parameterReference(parameterIndex) });
        fnProgram->runtimeParameters.push_back({ parameterKind, name, info.type, nameLoc });
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
        emitInitialize(arrowLoc, Expression::parameterReference(fnProgram->runtimeParameters.size()), takeTopExpression());

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
            VERIFY_NOT_REACHED();
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

void Generator::visitTypeImplDeclaration() {
    VERIFY(tok->kind() == Token::TypeImplDecl);
    advance();
    visitExpression();
    Constant implOf = expressionToConstant();
    VERIFY(implOf.kind() == ConstantKind::Parameterize);
    program->setImplConstant(implOf);
    auto base = asFoldBase(implOf);
    VERIFY(base.program->kind() == ProgramKind::Type);

    visitTypeMembers();
}

void Generator::visitTypeDeclaration() {
    VERIFY(tok->kind() == Token::TypeDecl);
    advance();
    visitTypeMembers();
}

void Generator::visitTypeMembers() {
    VERIFY(program->kind() == ProgramKind::Type);
    auto* typeProgram = cast<TypeProgram>(program);

    auto savedTok = tok;
    for (int_t i = 0; i < (int_t)typeProgram->runtimeParameters.size(); i++) {
        auto& member = typeProgram->runtimeParameters[i];
        VERIFY(member.kind() == RuntimeParameterKind::UncheckedMember);

        setParseLocation(member.parseLocation());
        VERIFY(tok->kind() == Token::MemberDecl || tok->kind() == Token::HasMemberDecl);
        VERIFY(tok->data() == Constant(ConstantKind::Invalid, i).toUint());
        RuntimeParameterKind newKind = tok->kind() == Token::HasMemberDecl ? RuntimeParameterKind::HasMember : RuntimeParameterKind::Member;
        advance();

        SourceLocation conversionLocation = tok->location(); // TODO: Should be the ':' for member declrations
        visitExpression();
        contextualToType(conversionLocation);
        Type type = verifyType(expressionToConstant());

        member = RuntimeParameter(newKind, member.name, type, member.location());
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
        auto expectedCategory = expectedInitializerCategory(tok->kind());
        Word name = Word::fromUint(tok->data());
        SourceLocation nameLoc = tok->location();
        advance();
        auto info = visitVariableDeclaration(expectedCategory, false);
        declareLocalVariable(name, nameLoc, info);
    } else if (tok->kind() == Token::DestroyStmt) {
        SourceLocation location = tok->location();
        advance();
        visitExpression();
        auto expr = takeTopExpression();

        VERIFY(expr.kind() == ExpressionKind::VariableReference || expr.kind() == ExpressionKind::ParameterReference);
        localState.setActive(expr, false);
        emitDeactivate(location, expr);
    } else if (tok->kind() == Token::DiscardStmt) {
        SourceLocation location = tok->location();
        advance();
        visitExpression();
        auto expr = takeTopExpression();

        VERIFY(expr.kind() == ExpressionKind::ReferenceReference);
        localState.setActive(expr, false);
        emitDeactivate(location, expr);
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
            auto argumentNames = context.parseOutput.argumentNames(tok->data());
            advance();
            generateParameterizeExpr(argumentNames);
        } else if (tok->kind() == Token::CallExpr) {
            CallTarget target = resolveCallTarget(context.parseOutput.argumentNames(tok->data()));
            SourceLocation callLoc = tok->location();
            advance();
            generateCallExpr(callLoc, std::move(target));
        } else if (tok->kind() == Token::StaticAccessExpr) {
            generateStaticAccessExpr();
            advance();
        } else if (tok->kind() == Token::MemberAccessExpr) {
            generateMemberAccessExpr();
            advance();
        } else {
            break;
        }
    }
}

void Generator::visitPrimaryExpr() {
    if (tok->kind() == Token::IdentifierExpr) {
        generateIdentifierExpr();
        advance();
    } else {
        VERIFY_NOT_REACHED();
    }
}

}