#include "semantic.h"
#include "NodeStreamVisitor.h"
#include <ranges>

// For 'dereference(inout self) <=> *stored_id;' we need to effectively evaluate the dereference
// of 'stored_id' at the call site.

// All literals of type 'type' are decls.

template<Opcode op, typename... Args>
auto InstructionStream::emit(Args... args) {

#define INST(name, layout)                           \
    if constexpr (op == Opcode::name) {              \
        return emit_##layout(Opcode::name, args...); \
    }
    ENUMERATE_INSTRUCTIONS
#undef INST
}

SSAName InstructionStream::allocateName() {
    size_t id = definitions.size();
    size_t definition = stream.size();
    VERIFY(definition <= (uint16_t)-1);
    definitions.push_back(definition);
    return { stream_phase, id };
}

SSAName InstructionStream::emit_unary(Opcode op, SSAName in) {
    SSAName out = allocateName();
    stream.push_back({ op, localize(out), localize(in), Instruction::UNUSED_OPERAND });
    return out;
}
SSAName InstructionStream::emit_binary(Opcode op, SSAName in1, SSAName in2) {
    SSAName out = allocateName();
    stream.push_back({ op, localize(out), localize(in1), localize(in2) });
    return out;
}
SSAName InstructionStream::emit_foreign_constant(Opcode op, SSAName decl, ConstantStreamInstructionOperand constant) {
    SSAName out = allocateName();
    stream.push_back({ op, localize(out), localize(decl), constant });
    return out;
}
SSAName InstructionStream::emit_call(Opcode op, SSAName argsBase, uint16_t count) {
    SSAName out = allocateName();
    stream.push_back({ op, localize(out), localize(argsBase), count });
    return out;
}

SSAName ConstantTable::emit(TypedConstant constant) {
    VERIFY(encodedValues.size() == types.size());
    size_t id = encodedValues.size();
    encodedValues.push_back(constant.encodedValue);
    types.push_back(constant.type);
    return { table_phase, id };
}

static TypeDecl typeType { DeclKind::StructType, { words["type"], SingleTokenSourceRange() }, nullptr };
static TypeDecl functionLiteralType { DeclKind::StructType, { words["function_literal"], SingleTokenSourceRange() }, nullptr };

template<typename T = Decl>
struct DeclLiteral : SSAName {
    T* decl;
    constexpr DeclLiteral(SSAName value, T* decl)
        : SSAName(value), decl(decl) { }
    constexpr T* operator->() const { return decl; }
};

struct StmtResult { };
struct Generator : private NodeStreamVisitor<Generator, StmtResult, ExprValue> {
    friend NodeStreamVisitor<Generator, StmtResult, ExprValue>;
    Generator(SemanticContext* sema, ConstantTable* literalTable, InstructionStream* constantStream, InstructionStream* targetStream, Decl* containingScope)
        : sema(sema), literalTable(literalTable), constantStream(constantStream), targetStream(targetStream), containingScope(containingScope) { }

    SemanticContext* sema = nullptr;

    ConstantTable* literalTable = nullptr;
    InstructionStream* constantStream = nullptr;
    InstructionStream* targetStream = nullptr;
    Decl* containingScope = nullptr;

    std::optional<ExprValue> visitExpression(Node* node) { return visitExpr(node); }
    void visitBody(Node* node) {
        visitGeneric(node);
    }

private:
    template<Opcode op, typename... Args>
    SSAName emit(InstructionStream* out, Args... args) {
        (transformEmitArgument(out, args), ...);
        return out->emit<op>(args...);
    }

    // statements
    StmtResult visitExpressionStmt(ExpressionStmt&, ExprValue) {
        return {};
    }
    StmtResult visitUpdateStmt(UpdateStmt&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitLetStmt(LetStmt&) { VERIFY_NOT_REACHED(); }
    StmtResult visitCompoundStmt(CompoundStmt&) {
        while (visitStmt().has_value()) { }
        return {};
    }
    StmtResult visitIfStmt(IfStmt&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitReturnStmt(ReturnStmt&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitEmptyReturnStmt(EmptyReturnStmt&) {
        VERIFY_NOT_REACHED();
    }

    // expressions
    ExprValue visitUnaryOperatorExpr(UnaryOperatorExpr&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitBinaryOperatorExpr(BinaryOperatorExpr&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitCallExpr(CallExpr&, ExprValue base) {
        // Because of overload resolution we don't know which implicit conversions to apply after
        // looking at just a subset of arguments. There are two kinds of solutions here:
        //  1. Don't allow implicit conversions and overload resolution at same time. So we either
        //     perform implicit conversions on an not overloaded set OR call an overload without
        //     applying any conversions.
        //  + Simplicity
        //  + Fewer instruction
        //  + There is no 'best match' logic required
        //  2. We emit extra 'nop's after evaluating each argument that we later be retroactively
        //     turn into calls to the required conversion functions.
        //  + Can handle operator overloading
        // We can also combine these approaches in several ways:
        //  3. By only applying user-defined implicit conversions if there is a Rust like '.into()'.
        //  4. By considering argument designations and argument count to resolve the callee and
        //     only falling back to 1. if there still isn't a unique match.

        if (!literallyEqual(base.type(), &functionLiteralType)) {
            // TODO: handle more base types
            VERIFY_NOT_REACHED();
        }
        auto fnDecl = useDeclLiteral<FunctionDecl>(base.asValue());
        std::vector<SSAName> arguments;
        arguments.reserve(fnDecl->parameters.size());
        for (;;) {
            auto maybeArg = visitExpr(ExpressionPrecedence::Statement);
            if (!maybeArg.has_value())
                break;
            auto rawArg = maybeArg.value();
            auto param = fnDecl->parameters[arguments.size()];
            TypedValue paramType = { literalFold(fnDecl, param.type), typeTypeLiteral() };
            auto convertedArg = implicitConversion(paramType, rawArg);
            arguments.push_back(convertedArg);
        }
        VERIFY(arguments.size() == fnDecl->parameters.size());
        SSAName baseArg = emit<Opcode::Nop>(targetStream, base.asValue());
        for (int_t i = 0; i < (int_t)arguments.size(); i++)
            emit<Opcode::Nop>(targetStream, arguments[i]);

        SSAName callValue = emit<Opcode::Call>(targetStream, baseArg, arguments.size());
        if (fnDecl->returnType)
            return TypedValue { callValue, literalFold(fnDecl, *fnDecl->returnType) };
        return ExprValue::statement(); // FIXME: return unit type
    }
    ExprValue visitParenthesizedExpr(ParenthesizedExpr&) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitAccessExpr(AccessExpr&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitIdentifierExpr(IdentifierExpr& e) {
        Decl* decl = sema->lookupFromInside(containingScope, e.identifier());
        // The value of an IdentifierExpr:
        //  - For a templated-decl a literal for the template.
        //  - For a variable-decl the hypothetical value of the load.
        //  - For a type-decl a literal for the type.
        //  - For a function-decl a literal for the function.

        if (auto parameterizedDecl = dyn_cast<ParameterizedDecl>(decl)) {
            if (parameterizedDecl->parameterDecls()->templateParameterCount > 0) {
                // TODO: Handle templates
                VERIFY_NOT_REACHED();
            }
            if (isDeclType<TypeDecl>(decl->kind())) {
                SSAName declLit = literalTable->emit(decl);
                return TypedValue { declLit, typeTypeLiteral() };
            } else if (auto varDecl = dyn_cast<StaticVariableDecl>(decl)) {
                auto varDeclLit = useDeclLiteral<StaticVariableDecl>(literalTable->emit(varDecl.value()));
                SSAName type = literalFold(varDeclLit, varDecl->typeValue);
                if (varDecl->kind() == DeclKind::StaticLetVariable) {
                    SSAName constValue = literalFold(varDeclLit, varDecl->initValue);
                    return TypedValue { constValue, type };
                } else if (varDecl->kind() == DeclKind::StaticVarVariable) {
                    SSAName idValue = emit<Opcode::StaticVariableId>(constantStream, varDeclLit);
                    return ExprValue::load(idValue, type);
                } else
                    VERIFY_NOT_REACHED();
            } else if (auto fnDecl = dyn_cast<FunctionDecl>(decl)) {
                SSAName declLit = literalTable->emit(fnDecl.value());
                return TypedValue { declLit, functionLiteralTypeLiteral() };
            }
        }
        VERIFY_NOT_REACHED();
    }
    ExprValue visitCompoundExpr(CompoundExpr&) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitIfExpr(IfExpr&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitCommaElseExpr(CommaElseExpr&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitNumericLiteralExpr(NumericLiteralExpr&) { VERIFY_NOT_REACHED(); }
    ExprValue visitCharacterLiteralExpr(CharacterLiteralExpr&) { VERIFY_NOT_REACHED(); }
    ExprValue visitDesignateArgument(DesignateArgument&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitParameterize(Parameterize&, ExprValue) {
        VERIFY_NOT_REACHED();
    }

public:
    //
    SSAName typeTypeLiteral() {
        return literalTable->emit(&typeType);
    }
    SSAName functionLiteralTypeLiteral() {
        return literalTable->emit(&functionLiteralType);
    }
    bool literallyEqual(SSAName leftName, TypedConstant right) {
        if (leftName.phase() != ValuePhase::Literal)
            return false;
        TypedConstant left = literalTable->get(leftName);
        return compareConstantsOfSameType(left, right);
    }
    template<std::derived_from<ParameterizedDecl> T>
    SSAName literalFold(DeclLiteral<T> decl, ConstantStreamInstructionOperand constant) {
        if (constant.phase() == ValuePhase::Literal)
            return literalTable->emit(decl->program.literalTable.get(constant));
        return constantStream->emit<Opcode::ForeignConstant>(decl, constant);
    }

    TypedValue implicitToType(ExprValue in) {
        if (literallyEqual(in.type(), &typeType)) {
            return materialize(in);
        }
        // TODO: Implement more cases
        VERIFY_NOT_REACHED();
    }
    TypedValue implicitConversion(TypedValue targetType, ExprValue sourceValue) {
        VERIFY(literallyEqual(targetType.type(), &typeType));
        if (targetType.phase() == ValuePhase::Literal && sourceValue.type().phase() == ValuePhase::Literal) {
            if (compareConstantsOfSameType(literalTable->get(targetType), literalTable->get(sourceValue.type())))
                return materialize(sourceValue);
        }
        // TODO: Implement more cases

        // We only perform implicit conversions if the target type has no undeduced parameters.
        // In that case we emit 'source-value.(implicit_cast<target-type>::convert)()' if we have
        // seen it. Otherwise we emit 'proof typeof(source-value) == target-type'.
        VERIFY_NOT_REACHED();
    }

    TypedValue materializeIn(InstructionStream* stream, ExprValue in) {
        switch (in.kind()) {
        case ExprValueKind::Value:
            return in.asValue();
        case ExprValueKind::Load:
            return { emit<Opcode::Load>(stream, in.primary()), in.type() };
        default:
            VERIFY_NOT_REACHED();
        }
    }
    TypedValue materialize(ExprValue in) { return materializeIn(targetStream, in); }
    TypedValue materializeConstant(ExprValue in) { return materializeIn(constantStream, in); }

    TypedValue typeOf(ExprValue in) { return { in.type(), typeTypeLiteral() }; }
    TypedValue typeOf(TypedValue in) { return { in.type(), typeTypeLiteral() }; }

    DeclLiteral<> useDeclLiteral(SSAName lit) {
        VERIFY(lit.phase() == ValuePhase::Literal);
        Decl* decl = literalTable->get(lit).asDecl();
        sema->requireSignature(decl);
        return { lit, decl };
    }
    template<std::derived_from<Decl> T>
    DeclLiteral<T> useDeclLiteral(SSAName lit) {
        auto decl = useDeclLiteral(lit);
        return { (SSAName)decl, dyn_cast<T>(decl.decl) };
    }

    void transformEmitArgument(InstructionStream*, auto& arg)
        requires(!std::convertible_to<decltype(arg), SSAName>)
    { }
    void transformEmitArgument(InstructionStream* out, SSAName& name) {
        if (name.phase() == ValuePhase::Literal && out->stream_phase == ValuePhase::Runtime)
            name = emit<Opcode::Nop>(constantStream, name);
    }
};

id<Decl> SemanticContext::lookupFromInside(Decl* d, WordAndLocation name) {
    for (;;) {
        VERIFY(d != nullptr);
        switch (d->kind()) {

        case DeclKind::Namespace: {
            NamespaceDecl* decl = (NamespaceDecl*)d;
            auto staticDecls = decl->staticDecls()->decls(name);
            if (!staticDecls.empty())
                return *staticDecls.begin();
            d = decl->declaringDecl();
            continue;
        }

        case DeclKind::StructType:
        case DeclKind::ObjectType: {
            TypeDecl* decl = (TypeDecl*)d;
            Decl* parameterDecl = decl->parameterDecls()->find(name);
            if (parameterDecl)
                return parameterDecl;
            auto staticDecls = decl->staticDecls()->decls(name);
            if (!staticDecls.empty())
                return *staticDecls.begin();
            d = decl->declaringDecl();
            continue;
        }

        case DeclKind::Function:
        case DeclKind::StaticLetVariable:
        case DeclKind::StaticVarVariable: {
            ParameterizedDecl* decl = dyn_cast<ParameterizedDecl>(d);
            Decl* parameterDecl = decl->parameterDecls()->find(name);
            if (parameterDecl)
                return parameterDecl;
            d = ((StaticDecl*)d)->declaringDecl();
            continue;
        }

        default:
            VERIFY_NOT_REACHED();
        }
    }
}

void SemanticContext::requireSignature(Decl* d) {
    if (d->signatureChecked())
        return;
    if (d->status() == DeclStatus::SignatureCheckInProgress) {
        // TODO: Handle error
        VERIFY_NOT_REACHED();
    }
    if (auto decl = dyn_cast<TypeDecl>(d)) {
        signatureCheckTypeDecl(*decl);
    } else if (auto decl = dyn_cast<FunctionDecl>(d)) {
        signatureCheckFunctionDecl(*decl);
    } else if (auto decl = dyn_cast<StaticVariableDecl>(d)) {
        signatureCheckStaticVariableDecl(*decl);
    } else {
        VERIFY_NOT_REACHED();
    }
}

void SemanticContext::signatureCheckStaticVariableDecl(StaticVariableDecl& d) {
    VERIFY(d.status() == DeclStatus::Unchecked);
    d.setStatus(DeclStatus::SignatureCheckInProgress);

    Generator g(this, &d.program.literalTable, &d.program.constantStream, &d.program.constantStream, &d);
    auto typeExpr = g.visitExpression(d.typeExpr())
                        .transform([&g](ExprValue v) { return g.implicitToType(v); });

    auto initExpr = g.visitExpression(d.initExpr())
                        .or_else([] -> std::optional<ExprValue> {
                            // TODO: Handle error
                            VERIFY_NOT_REACHED();
                        })
                        .value();
    if (typeExpr.has_value()) {
        initExpr = g.implicitConversion(typeExpr.value(), initExpr);
    } else {
        typeExpr = g.typeOf(initExpr);
    }
    d.typeValue = typeExpr.value().localizeConstant();
    d.initValue = g.materialize(initExpr).localizeConstant();

    d.setStatus(DeclStatus::SignatureChecked);
}

void SemanticContext::signatureCheckTypeDecl(TypeDecl& d) {
    VERIFY(d.status() == DeclStatus::Unchecked);
    d.setStatus(DeclStatus::SignatureCheckInProgress);

    d.setStatus(DeclStatus::SignatureChecked);
}
void SemanticContext::signatureCheckFunctionDecl(FunctionDecl& d) {
    VERIFY(d.status() == DeclStatus::Unchecked);
    d.setStatus(DeclStatus::SignatureCheckInProgress);

    Generator g(this, &d.program.literalTable, &d.program.constantStream, &d.program.constantStream, &d);

    auto parameterDecls = d.parameterDecls()->parameters();
    VERIFY(d.parameterDecls()->templateParameterCount == 0);
    d.parameters.reserve(parameterDecls.size());
    for (ParameterOrMemberDecl* param : parameterDecls) {
        auto typeExpr = g.visitExpression(param->typeExpr());
        VERIFY(typeExpr.has_value());
        d.parameters.push_back({ param->name, kindToModel(param->kind()), g.implicitToType(typeExpr.value()).localizeConstant() });
    }
    auto returnTypeExpr = g.visitExpression(d.returnTypeExpr());
    if (returnTypeExpr.has_value())
        d.returnType = g.implicitToType(returnTypeExpr.value()).localizeConstant();

    d.setStatus(DeclStatus::SignatureChecked);
}
void SemanticContext::checkBody(FunctionDecl& d) {
    requireSignature(&d);

    Generator g(this, &d.program.literalTable, &d.program.constantStream, &d.program.runtimeStream, &d);
    g.visitBody(d.body());
}

void SemanticContext::check(StaticDeclContext* parent) {
    for (Decl* child : *parent) {
        if (auto decl = dyn_cast<NamespaceDecl>(child)) {
            check(decl->staticDecls());
        } else if (auto decl = dyn_cast<TypeDecl>(child)) {
            requireSignature(child);
            check(decl->staticDecls());
        } else if (auto decl = dyn_cast<FunctionDecl>(child)) {
            requireSignature(child);
            checkBody(*decl);
        } else {
            requireSignature(child);
        }

        if (auto decl = dyn_cast<ParameterizedDecl>(child)) {
            dump(child, *wordTable);
        }
    }
}