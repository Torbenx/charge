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
        case parse::TokenKind::ConstReferenceDecl:
            return ExpressionCategory::ConstReference;
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
    // emitControl(Opcode::BeginScope, location, 0, { .empty {} });
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
            auto ref = Reference::localVariable(localState.variableActiveMask.size() - 1);
            emitControl<DeactivateInstruction>(location, ref);
        }
        localState.variableActiveMask.pop_back();
        localVariables.pop_back();
    }
    VERIFY(localState.referenceActiveMask.size() == localReferences.size());
    while (localState.referenceActiveMask.size() > scope.localReferenceCount) {
        if (localState.referenceActiveMask.back()) {
            auto ref = Reference::localReference(localState.referenceActiveMask.size() - 1);
            emitControl<DeactivateInstruction>(location, ref);
        }
        localState.referenceActiveMask.pop_back();
        localReferences.pop_back();
    }

    // emitControl(Opcode::EndScope, location, 0, { .empty {} });
}

void Generator::declareLocalVariable(Word name, SourceLocation location, VariableDeclaration declaration) {
    VERIFY(localVariables.size() == localState.variableActiveMask.size());
    int_t index = localVariables.size();
    localVariables.push_back({ declaration.type });
    localState.variableActiveMask.push_back(declaration.hasInitializer);
    localLookupEntries.push_back({ name, Reference::localVariable(index) });

    if (declaration.hasInitializer)
        emitControl<InitializeInstruction>(location, Reference::localVariable(index), takeTopExpression());
}

void Generator::visitDeclaration() {
    if (tok->kind() == Token::TemplateAttribute) {
        visitTemplateParameters();
    }
    if (tok->kind() == Token::ObjectTypeDecl || tok->kind() == Token::StructTypeDecl) {
        visitTypeDeclaration();
    } else if (tok->kind() == Token::VarValueDecl || tok->kind() == Token::LetValueDecl) {
        visitStaticVariableDeclaration();
    } else if (tok->kind() == Token::FunctionDecl) {
        visitFunctionDeclaration();
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
        VERIFY(!expressionStack.empty());

        DeductionState state(program, programHandle, parameterTypes.size());
        state.copyParameters(program->parameters.size());
        initialize(assignLocation, state, expectedCategory, type);
        VERIFY(state.isComplete());
        type = verifyType(fold(state.toFoldBase(INVALID_CONSTANT), type));

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
    VERIFY(tok->kind() == Token::LetValueDecl || tok->kind() == Token::VarValueDecl);
    advance();

    auto info = visitVariableDeclaration(ExpressionCategory::Value, false);
    VERIFY(info.hasInitializer);
    program->setType(info.type);
    if (program->kind() == ProgramKind::Value) {
        auto* valueProgram = cast<ValueProgram>(program);
        valueProgram->setValue(expressionToConstant());
    } else {
        VERIFY_NOT_REACHED();
    }
}

void Generator::visitFunctionDeclaration() {
    VERIFY(program->kind() == ProgramKind::Function);
    auto* fnProgram = cast<FunctionProgram>(program);

    VERIFY(tok->kind() == Token::FunctionDecl);
    advance();

    int_t scratchSizeAtBegin = instructionScratch.size();

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
        case Token::ConstReferenceDecl:
            return RuntimeParameterKind::ConstReference;
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
        localLookupEntries.push_back({ name, Reference(ReferenceKind::Parameter, parameterIndex) });
        fnProgram->runtimeParameters.push_back({ parameterKind, name, info.type, nameLoc });
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();

    if (tok->kind() == Token::BodyExpr) {
        advance();
        visitExpression();
        VERIFY(tok->kind() == Token::ExpressionStmt);
        advance();

        program->setType(resultType(topExpression()));
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
        advance();
        visitStatement();
    }
    fnProgram->setBody({ instructionScratch.begin() + scratchSizeAtBegin, instructionScratch.end() });
    instructionScratch.erase(instructionScratch.begin() + scratchSizeAtBegin, instructionScratch.end());
}

void Generator::visitTypeDeclaration() {
    VERIFY(program->kind() == ProgramKind::Type);
    auto* typeProgram = cast<TypeProgram>(program);

    VERIFY(tok->kind() == Token::StructTypeDecl || tok->kind() == Token::ObjectTypeDecl);
    advance();

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
        auto scope = beginLocalScope(tok->location());
        advance();
        while (tok->kind() != Token::EmptyNode) {
            visitStatement();
        }
        VERIFY(tok->kind() == Token::EmptyNode);
        endLocalScope(scope, tok->location());
        advance();
    } else if (tok->kind() == Token::LetValueDecl || tok->kind() == Token::VarValueDecl
        || tok->kind() == Token::UniqueReferenceDecl || tok->kind() == Token::SharedReferenceDecl
        || tok->kind() == Token::ConstReferenceDecl) {
        auto expectedCategory = expectedInitializerCategory(tok->kind());
        Word name = Word::fromUint(tok->data());
        SourceLocation nameLoc = tok->location();
        advance();
        auto info = visitVariableDeclaration(expectedCategory, false);
        declareLocalVariable(name, nameLoc, info);
    } else if (tok->kind() == Token::DestroyStmt || tok->kind() == Token::DiscardStmt) {
        bool isDiscard = tok->kind() == Token::DiscardStmt;
        SourceLocation location = tok->location();
        advance();

        visitExpression();
        auto expr = topExpression();
        takeTopExpression();
        VERIFY(expr.isReference());
        auto reference = expr.reference();

        VERIFY(reference.kind() != ReferenceKind::MemberExpression);
        if (reference.kind() == ReferenceKind::LocalReference)
            VERIFY(isDiscard);
        else
            VERIFY(!isDiscard);
        emitControl<DeactivateInstruction>(location, reference);
        localState.setActive(reference, false);
    } else {
        visitExpression();
        if (tok->kind() == Token::IfStmt) {
            SourceLocation ifLoc = tok->location();
            auto* branchInst = emitControl<BranchInstruction>(ifLoc);

            advance();
            contextualToBool(ifLoc);

            OwnedExpressionResult condition = takeTopExpression();

            expressionStack.push_back({ .startOffset = (uint32_t)instructionScratch.size() });
            auto ifScope = beginLocalScope(ifLoc);
            visitStatement();
            endLocalScope(ifScope, {});

            {
                auto newEnd = instructionScratch.begin() + expressionStack.back().startOffset;
                branchInst->addBranch(std::move(condition), { newEnd, instructionScratch.end() });
                instructionScratch.erase(newEnd, instructionScratch.end());
            }

            if (tok->kind() == Token::ElseStmt) {
                SourceLocation elseLoc = tok->location();
                advance();

                expressionStack.push_back({ .startOffset = (uint32_t)instructionScratch.size() });
                auto elseScope = beginLocalScope(elseLoc);
                visitStatement();
                endLocalScope(elseScope, {});

                {
                    auto newEnd = instructionScratch.begin() + expressionStack.back().startOffset;
                    branchInst->addBranch(builtins::true_constant, { newEnd, instructionScratch.end() });
                    instructionScratch.erase(newEnd, instructionScratch.end());
                }
            }
        } else {
            VERIFY(tok->kind() == Token::ExpressionStmt);
            if (!topExpression().isReference()) {
                emitControl<DiscardInstruction>(tok->location(), takeTopExpression());
            }
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
            advance();
            generateCallExpr(std::move(target));
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