#include <sema/Context.h>
#include <sema/Generator.h>

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

void Generator::declareLocalVariable(Word name, SourceLocation location, VariableDeclaration declaration) {
    VERIFY(localVariables.size() == currentScope.variableActiveMask.size());
    int_t index = localVariables.size();
    emitControl(
        Opcode::VarDecl, location, declaration.hasInitializer ? 1 : 0,
        { .decl = { .type = declaration.type, .localValueIndex = (uint32_t)index } });
    localVariables.push_back({ declaration.type });
    currentScope.variableActiveMask.push_back(declaration.hasInitializer);
    localLookupEntries.push_back({ name, ReferenceExpression(ReferenceExpressionKind::LocalVariable, index) });
}

void Generator::visitDeclaration() {
    if (tok->kind() == Token::TemplateAttribute) {
        visitTemplateParameters();
    }
    if (tok->kind() == Token::ObjectTypeDecl || tok->kind() == Token::StructTypeDecl) {
        visitTypeDeclaration();
    } else if (tok->kind() == Token::StaticLetDecl || tok->kind() == Token::StaticVarDecl) {
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
        parameterTypes.push_back(verifyType((Value)explicitParameter.type));
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();
}

Generator::VariableDeclaration Generator::visitVariableDeclaration(bool programParameters) {
    std::optional<Type> variableType;
    VERIFY(parameterTypes.size() == program->parameters.size());
    if (tok->kind() != Token::AssignStmt) {
        // parse type
        wildcardMeaning = WildcardMeaning::ImplicitTemplate;
        visitExpression();
        contextualToType();
        variableType = verifyType(makeExpressionValue());
        wildcardMeaning = WildcardMeaning::Error;
    } else {
        variableType = verifyType(newImplicitParameter(builtins::type_type));
    }
    Type type = variableType.value();
    VERIFY(tok->kind() == Token::AssignStmt);
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
        state.identityMap(program->parameters.size());
        implicitCastTo(state, type);
        VERIFY(state.isComplete());
        type = verifyType(fold(state.toFoldBase(INVALID_VALUE), type));

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
    if (tok->kind() != Token::ImplicitKindParameter) {
        // report error
        VERIFY_NOT_REACHED();
    }
    Word name = Word::fromUint(tok->data());
    advance();

    auto info = visitVariableDeclaration(true);
    std::optional<Value> initializer;
    if (info.hasInitializer)
        initializer = makeExpressionValue();
    return { name, info.type, initializer };
}

void Generator::visitStaticVariableDeclaration() {
    VERIFY(tok->kind() == Token::StaticLetDecl || tok->kind() == Token::StaticVarDecl);
    advance();

    auto info = visitVariableDeclaration(false);
    VERIFY(info.hasInitializer);
    program->setType(info.type);
    if (program->kind() == ProgramKind::Value) {
        auto* valueProgram = cast<ValueProgram>(program);
        valueProgram->setValue(makeExpressionValue());
    }
}

void Generator::visitFunctionDeclaration() {
    VERIFY(program->kind() == ProgramKind::Function);
    auto* fnProgram = cast<FunctionProgram>(program);

    VERIFY(tok->kind() == Token::FunctionDecl);
    advance();

    int_t scratchSizeAtBegin = instructionScratch.size();

    lookupStack.push_back(LookupContext::forLocal(this));

    while (tok->kind() != Token::EmptyNode) {
        VERIFY(tok->kind() == Token::ImplicitKindParameter);
        Word name = Word::fromUint(tok->data());
        auto nameLoc = tok->location();
        advance();
        auto info = visitVariableDeclaration(true);

        VERIFY(fnProgram->runtimeParameters.size() == currentScope.parameterActiveMask.size());
        int_t parameterIndex = fnProgram->runtimeParameters.size();
        currentScope.parameterActiveMask.push_back(true);
        localLookupEntries.push_back({ name, ReferenceExpression(ReferenceExpressionKind::Parameter, parameterIndex) });
        fnProgram->runtimeParameters.push_back({ RuntimeParameterKind::LetParameter, name, info.type, nameLoc });
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();

    if (tok->kind() == Token::BodyExpr) {
        advance();
        visitExpression();
        VERIFY(tok->kind() == Token::ExpressionStmt);
        advance();

        program->setType(topExpression().type());
    } else {
        if (tok->kind() == Token::ReturnType) {
            advance();
            visitExpression();
            program->setType(verifyType(makeExpressionValue()));
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
        VERIFY(tok->data() == Value(ValueKind::Invalid, i).toUint());
        RuntimeParameterKind newKind = tok->kind() == Token::HasMemberDecl ? RuntimeParameterKind::HasMember : RuntimeParameterKind::Member;
        advance();

        visitExpression();
        contextualToType();
        Type type = verifyType(makeExpressionValue());

        member = RuntimeParameter(newKind, member.name, type, member.location());
    }

    program->setType(verifyType(makeParameterize(programHandle, identityParameterMap(program))));
    tok = savedTok;
}

void Generator::visitStatement() {
    if (tok->kind() == Token::CompoundStmt) {
        SourceLocation openBraceLoc = tok->location();
        advance();
        while (tok->kind() != Token::EmptyNode) {
            visitStatement();
        }
        VERIFY(tok->kind() == Token::EmptyNode);
        advance();
        emitControl(Opcode::EndScope, openBraceLoc, 0, { .empty {} });
    } else if (tok->kind() == Token::LetStmt || tok->kind() == Token::VarStmt) {
        Word name = Word::fromUint(tok->data());
        SourceLocation nameLoc = tok->location();
        advance();
        auto info = visitVariableDeclaration(false);
        declareLocalVariable(name, nameLoc, info);
    } else if (tok->kind() == Token::DestroyStmt || tok->kind() == Token::DiscardStmt) {
        bool isDiscard = tok->kind() == Token::DiscardStmt;
        SourceLocation location = tok->location();
        advance();

        visitExpression();
        auto expr = topExpression();
        popExpression();
        VERIFY(expr.category() == InstructionCategory::LValue);
        VERIFY(expr.opcode() == Opcode::Reference);
        auto reference = expr.data().referenceExpr;

        VERIFY(reference.kind() != ReferenceExpressionKind::MemberExpression);
        if (reference.kind() == ReferenceExpressionKind::LocalReference)
            VERIFY(isDiscard);
        else
            VERIFY(!isDiscard);
        emitControl(Opcode::Deactivate, location, 0, { .deactiveTarget = reference });
        currentScope.setActive(reference, false);
    } else {
        visitExpression();
        if (tok->kind() == Token::IfStmt) {
            SourceLocation ifLoc = tok->location();
            advance();
            contextualToBool();

            auto notTrueJump = emitJumpIf(ifLoc);
            visitStatement();

            if (tok->kind() == Token::ElseStmt) {
                auto skipElseJump = emitJump(tok->location());
                advance();

                linkToNextInstruction(notTrueJump);
                visitStatement();

                linkToNextInstruction(skipElseJump);
            } else {
                linkToNextInstruction(notTrueJump);
            }
        } else {
            VERIFY(tok->kind() == Token::ExpressionStmt);
            emitControl(Opcode::Discard, tok->location(), 1, { .empty {} });
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