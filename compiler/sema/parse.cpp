#include <sema/Context.h>
#include <sema/Generator.h>

namespace sema {

Generator::Generator(Context& context, ProgramHandle handle)
    : context(context), program(context.program(handle)), programHandle(handle) { }

void Generator::advance() { tok += 1; }

void Generator::declareLocal(Word name, SourceLocation location, VariableDeclaration declaration) {
    int_t index = localValues.size();
    emitNode(
        NodeKind::LetDecl, location, declaration.hasInitializer ? 1 : 0,
        NodeData { .decl = { .type = declaration.type, .localValueIndex = (uint32_t)index } });
    localValues.push_back({ declaration.type });
    localLookupEntries.push_back({ name, (uint32_t)index });
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
    Type type;
    int_t oldParameterCount = parameterTypes.size();
    if (programParameters)
        VERIFY(oldParameterCount == (int_t)program->parameters.size());
    if (tok->kind() != Token::AssignStmt) {
        // parse type
        wildcardMeaning = WildcardMeaning::ImplicitTemplate;
        visitExpression();
        implicitToType();
        type = verifyType(makeExpressionValue());
        wildcardMeaning = WildcardMeaning::Error;
    } else {
        type = verifyType(newImplicitParameter(builtins::type_type));
    }
    VERIFY(tok->kind() == Token::AssignStmt);
    advance();

    bool hasInitializer = false;
    if (tok->kind() != Token::ExpressionStmt) {
        if (oldParameterCount != (int_t)parameterTypes.size() && programParameters) {
            // 'type' contains implicitly created parameters.
            // Converting the initializer to 'type' can never happend without deducing them.
            VERIFY_NOT_REACHED();
        }
        visitExpression();

        DeductionState state(program, programHandle, parameterTypes.size());
        state.identityMap(oldParameterCount);
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
    VERIFY(program->kind() == ProgramKind::Value);
    auto* valueProgram = static_cast<ValueProgram*>(program);

    VERIFY(tok->kind() == Token::StaticLetDecl || tok->kind() == Token::StaticVarDecl);
    bool isVar = tok->kind() == Token::StaticVarDecl;
    advance();

    auto info = visitVariableDeclaration(false);
    VERIFY(info.hasInitializer);
    program->setType(info.type);
    valueProgram->setValue(makeExpressionValue());
}

void Generator::visitFunctionDeclaration() {
    VERIFY(program->kind() == ProgramKind::Function);
    auto* fnProgram = static_cast<FunctionProgram*>(program);

    VERIFY(tok->kind() == Token::FunctionDecl);
    advance();

    int_t stackSizeAtBegin = nodeStack.size();

    lookupStack.push_back(LookupContext::forLocal(this));

    while (tok->kind() != Token::EmptyNode) {
        VERIFY(tok->kind() == Token::ImplicitKindParameter);
        Word name = Word::fromUint(tok->data());
        auto nameLoc = tok->location();
        advance();
        auto info = visitVariableDeclaration(true);
        declareLocal(name, nameLoc, info);
        fnProgram->runtimeParameters.push_back({ RuntimeParameterKind::LetParameter, name, info.type, nameLoc });
    }
    VERIFY(tok->kind() == Token::EmptyNode);
    advance();

    if (tok->kind() == Token::BodyExpr) {
        advance();
        visitExpression();
        program->setType(topExpression().type());
        VERIFY(tok->kind() == Token::ExpressionStmt);
        emitNode(NodeKind::Function, {}, nodeStack.size() - stackSizeAtBegin, NodeData { .empty {} });
        VERIFY((int_t)nodeStack.size() == stackSizeAtBegin + 1);
        fnProgram->setBody(topNode());
        popNode();
    } else {
        std::optional<Type> type;
        if (tok->kind() == Token::ReturnType) {
            advance();
            visitExpression();
            type = verifyType(makeExpressionValue());
        }
        VERIFY(tok->kind() == Token::FunctionBody);
        // ... visit statement
        VERIFY_NOT_REACHED();
    }
}

void Generator::visitTypeDeclaration() {
    VERIFY(program->kind() == ProgramKind::Type);
    auto* typeProgram = static_cast<TypeProgram*>(program);

    VERIFY(tok->kind() == Token::StructTypeDecl || tok->kind() == Token::ObjectTypeDecl);
    advance();

    for (int_t i = 0; i < (int_t)typeProgram->runtimeParameters.size(); i++) {
        auto& member = typeProgram->runtimeParameters[i];
        VERIFY(member.kind() == RuntimeParameterKind::UncheckedMember);

        ParseScope parseScope(this, member.parseLocation());
        VERIFY(tok->kind() == Token::MemberDecl || tok->kind() == Token::HasMemberDecl);
        VERIFY(tok->data() == Value(ValueKind::Invalid, i).toUint());
        RuntimeParameterKind newKind = tok->kind() == Token::HasMemberDecl ? RuntimeParameterKind::HasMember : RuntimeParameterKind::Member;
        advance();

        visitExpression();
        implicitToType();
        Type type = verifyType(makeExpressionValue());

        member = RuntimeParameter(newKind, member.name, type, member.location());
    }

    program->setType(verifyType(makeParameterize(programHandle, identityParameterMap(program))));
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
            generateParameterizeExpr(visitExpressionList());
        } else if (tok->kind() == Token::CallExpr) {
            CallTarget target = resolveCallTarget();
            generateCallExpr(std::move(target), visitExpressionList());
        } else if (tok->kind() == Token::StaticAccessExpr) {
            generateStaticAccessExpr();
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

int_t Generator::visitExpressionList() {
    advance();
    int_t argumentCount = 0;
    while (tok->kind() != Token::EmptyNode) {
        argumentCount += 1;
        visitExpression();
    }
    advance();
    return argumentCount;
}

}