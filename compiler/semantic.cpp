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
static TypeDecl voidType { DeclKind::StructType, { words["void"], SingleTokenSourceRange() }, nullptr };

template<typename T = Decl>
struct DeclLiteral : SSAName {
    T* decl;
    constexpr DeclLiteral(SSAName value, T* decl)
        : SSAName(value), decl(decl) { }
    constexpr T* operator->() const { return decl; }
};

struct StmtResult { };
struct Generator : private NodeStreamVisitor<Generator, StmtResult, OwnedValue> {
    friend NodeStreamVisitor<Generator, StmtResult, OwnedValue>;
    Generator(SemanticContext* sema, ConstantTable* literalTable, InstructionStream* constantStream, InstructionStream* targetStream, Decl* containingScope)
        : sema(sema), literalTable(literalTable), constantStream(constantStream), targetStream(targetStream), containingScope(containingScope) { }

    SemanticContext* sema = nullptr;

    ConstantTable* literalTable = nullptr;
    InstructionStream* constantStream = nullptr;
    InstructionStream* targetStream = nullptr;
    Decl* containingScope = nullptr;

    std::optional<OwnedValue> visitExpression(Node* node) { return visitExpr(node); }
    void visitBody(Node* node) {
        visitGeneric(node);
    }

private:
    void transformEmitArgumentForTargetStream(auto& arg)
        requires(!std::convertible_to<decltype(arg), SSAName>)
    { }
    void transformEmitArgumentForTargetStream(SSAName& name) {
        if (name.phase() == ValuePhase::Literal && targetStream->stream_phase == ValuePhase::Runtime)
            name = constantStream->emit<Opcode::Nop>(name);
    }
    template<Opcode op, typename... Args>
    SSAName emit(Args... args) {
        (transformEmitArgumentForTargetStream(args), ...);
        return targetStream->emit<op>(args...);
    }

    // statements
    StmtResult visitExpressionStmt(ExpressionStmt&, OwnedValue) {
        return {};
    }
    StmtResult visitUpdateStmt(UpdateStmt&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitLetStmt(LetStmt&) { VERIFY_NOT_REACHED(); }
    StmtResult visitCompoundStmt(CompoundStmt&) {
        while (visitStmt().has_value()) { }
        return {};
    }
    StmtResult visitIfStmt(IfStmt&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitReturnStmt(ReturnStmt&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitEmptyReturnStmt(EmptyReturnStmt&) {
        VERIFY_NOT_REACHED();
    }

    // expressions
    OwnedValue visitUnaryOperatorExpr(UnaryOperatorExpr&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitBinaryOperatorExpr(BinaryOperatorExpr&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitCallExpr(CallExpr&, OwnedValue base) {
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
        auto fnDecl = useDeclLiteral<FunctionDecl>(base.asPureValue());
        std::vector<SSAName> arguments;
        arguments.reserve(fnDecl->parameters.size());
        std::vector<OwnedValue> temporaryAllocations;
        for (;;) {
            auto maybeArg = visitExpr(ExpressionPrecedence::Statement);
            if (!maybeArg.has_value())
                break;
            OwnedValue rawArg = std::move(maybeArg.value());
            auto param = fnDecl->parameters[arguments.size()];
            Type paramType = (Type)literalFold(fnDecl, param.type);
            matchTypes(paramType, rawArg.type());

            SSAName argumentSubstance;
            switch (rawArg.category()) {
            case ValueCategory::PValue: {
                if (param.model == ParameterModel::InOut || param.model == ParameterModel::Out) {
                    // TODO: Handle error
                    VERIFY_NOT_REACHED();
                }
                OwnedValue temporary = allocateRValue(rawArg.type());
                argumentSubstance = temporary.primary();
                temporaryAllocations.push_back(std::move(temporary));
                emit<Opcode::Store>(argumentSubstance, rawArg.asPureValue());
                break;
            }
            case ValueCategory::LValue: {
                if (param.model == ParameterModel::In || param.model == ParameterModel::InOut) {
                    argumentSubstance = rawArg.primary();
                    // TODO: copy if value type
                } else {
                    // TODO: Handle error
                    VERIFY_NOT_REACHED();
                }
                break;
            }
            case ValueCategory::RValue: {
                if (param.model == ParameterModel::In) {
                    requireValueType(paramType);
                } else if (param.model == ParameterModel::InOut || param.model == ParameterModel::Out) {
                    // TODO: Handle error
                    VERIFY_NOT_REACHED();
                }
                argumentSubstance = rawArg.primary();
                temporaryAllocations.push_back(std::move(rawArg));
                break;
            }
            default:
                VERIFY_NOT_REACHED();
            }
            arguments.push_back(argumentSubstance);
        }
        VERIFY(arguments.size() == fnDecl->parameters.size());
        for (int_t i = 0; i < (int_t)arguments.size(); i++)
            emit<Opcode::Nop>(arguments[i]);

        SSAName callValue = emit<Opcode::Call>(base.asPureValue(), arguments.size());
        for (OwnedValue& tmp : temporaryAllocations) {
            deallocateRValue(std::move(tmp));
        }
        return PureValue { callValue, (Type)literalFold(fnDecl, fnDecl->returnType) };
    }
    OwnedValue visitParenthesizedExpr(ParenthesizedExpr&) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitAccessExpr(AccessExpr&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitIdentifierExpr(IdentifierExpr& e) {
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
                return PureValue { declLit, typeTypeLiteral() };
            } else if (auto varDecl = dyn_cast<StaticVariableDecl>(decl)) {
                auto varDeclLit = useDeclLiteral<StaticVariableDecl>(literalTable->emit(varDecl.value()));
                Type type = (Type)literalFold(varDeclLit, varDecl->typeValue);
                if (varDecl->kind() == DeclKind::StaticLetVariable) {
                    SSAName constValue = literalFold(varDeclLit, varDecl->initValue);
                    requireValueType(type);
                    return PureValue { constValue, type };
                } else if (varDecl->kind() == DeclKind::StaticVarVariable) {
                    SSAName idValue = emit<Opcode::StaticVariableId>(varDeclLit);
                    return Value::lvalue(idValue, type);
                } else
                    VERIFY_NOT_REACHED();
            } else if (auto fnDecl = dyn_cast<FunctionDecl>(decl)) {
                SSAName declLit = literalTable->emit(fnDecl.value());
                return PureValue { declLit, functionLiteralTypeLiteral() };
            }
        }
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitCompoundExpr(CompoundExpr&) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitIfExpr(IfExpr&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitCommaElseExpr(CommaElseExpr&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitNumericLiteralExpr(NumericLiteralExpr&) { VERIFY_NOT_REACHED(); }
    OwnedValue visitCharacterLiteralExpr(CharacterLiteralExpr&) { VERIFY_NOT_REACHED(); }
    OwnedValue visitDesignateArgument(DesignateArgument&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitParameterize(Parameterize&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }

public:
    //
    Type typeTypeLiteral() {
        return (Type)literalTable->emit(&typeType);
    }
    Type functionLiteralTypeLiteral() {
        return (Type)literalTable->emit(&functionLiteralType);
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
            return literalTable->emit(decl->program.literalTable.get((SSAName)constant));
        return constantStream->emit<Opcode::ForeignConstant>(decl, constant);
    }

    Type implicitToType(OwnedValue in) {
        if (literallyEqual(in.type(), &typeType)) {
            return (Type)materialize(std::move(in));
        }
        // TODO: Implement more cases
        VERIFY_NOT_REACHED();
    }
    OwnedValue implicitConversion(Type targetType, OwnedValue sourceValue) {
        if (targetType.phase() == ValuePhase::Literal && sourceValue.type().phase() == ValuePhase::Literal) {
            if (compareConstantsOfSameType(literalTable->get(targetType), literalTable->get(sourceValue.type()))) {
                return sourceValue;
            }
        }
        // TODO: Implement more cases

        // We only perform implicit conversions if the target type has no undeduced parameters.
        // In that case we emit 'source-value.(implicit_cast<target-type>::convert)()' if we have
        // seen it. Otherwise we emit 'proof typeof(source-value) == target-type'.
        VERIFY_NOT_REACHED();
    }

    PureValue materialize(OwnedValue in) {
        switch (in.category()) {
        case ValueCategory::PValue:
            return in.asPureValue();
        case ValueCategory::LValue:
            requireValueType(in.type());
            return { emit<Opcode::Load>(in.primary()), in.type() };
        case ValueCategory::RValue: {
            requireValueType(in.type());
            PureValue out = { emit<Opcode::Load>(in.primary()), in.type() };
            deallocateRValue(std::move(in));
            return out;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    OwnedValue allocateRValue(Type type) {
        SSAName storage = emit<Opcode::Allocate>(type);
        return OwnedValue::rvalue(storage, type);
    }
    void deallocateRValue(OwnedValue value) {
        Value v = value.releaseValue();
        VERIFY(v.category() == ValueCategory::RValue);
        emit<Opcode::Deallocate>(v.type(), v.primary());
    }

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

    void requireValueType(Type) { }

    void matchTypes(Type target, Type source) {
        VERIFY(target.phase() == ValuePhase::Literal && source.phase() == ValuePhase::Literal);
        VERIFY(compareConstantsOfSameType(literalTable->get(target), literalTable->get(source)));
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
                        .transform([&g](OwnedValue v) { return g.implicitToType(std::move(v)); });

    auto initExpr = g.visitExpression(d.initExpr())
                        .or_else([] -> std::optional<OwnedValue> {
                            // TODO: Handle error
                            VERIFY_NOT_REACHED();
                        })
                        .value();
    if (typeExpr.has_value()) {
        g.matchTypes(typeExpr.value(), initExpr.type());
    } else {
        typeExpr = initExpr.type();
    }
    d.typeValue = typeExpr.value().localizeConstant();
    // FIXME: This only works for value types.
    d.initValue = g.materialize(std::move(initExpr)).localizeConstant();

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
        d.parameters.push_back({ param->name, kindToModel(param->kind()),
            g.implicitToType(std::move(typeExpr.value())).localizeConstant() });
    }
    auto returnTypeExpr = g.visitExpression(d.returnTypeExpr());
    if (returnTypeExpr.has_value())
        d.returnType = g.implicitToType(std::move(returnTypeExpr.value())).localizeConstant();
    else
        d.returnType = d.program.literalTable.emit(&voidType).localizeConstant();

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