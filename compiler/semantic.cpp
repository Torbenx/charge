#include "semantic.h"
#include "NodeStreamVisitor.h"
#include <ranges>

// For 'dereference(inout self) <=> *stored_id;' we need to effectively evaluate the dereference
// of 'stored_id' at the call site.

// All literals of type 'type' are decls.

#define DEFINE_DECL_PROGRAM(decl)                                             \
    constinit const uint32_t decl::DECL_PROGRAM_SIZE = sizeof(decl::Program); \
    decl::Program* decl::program() { return static_cast<decl::Program*>(ParameterizedDecl::program()); }

DEFINE_DECL_PROGRAM(TypeDecl)
DEFINE_DECL_PROGRAM(FunctionDecl)
DEFINE_DECL_PROGRAM(StaticVariableDecl)

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

SSAName InstructionStream::emit_nullary(Opcode op) {
    SSAName out = allocateName();
    stream.push_back({ op, localize(out), Instruction::UNUSED_OPERAND, Instruction::UNUSED_OPERAND });
    return out;
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
SSAName InstructionStream::emit_foreign_constant(Opcode op, SSAName decl, ConstantStreamOperand constant) {
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

static TypeDecl typeType { nullptr, DeclKind::StructType, { words["type"], SingleTokenSourceRange() }, nullptr };
static TypeDecl functionLiteralType { nullptr, DeclKind::StructType, { words["function_literal"], SingleTokenSourceRange() }, nullptr };
static TypeDecl voidType { nullptr, DeclKind::StructType, { words["void"], SingleTokenSourceRange() }, nullptr };

template<typename T = Decl>
    requires requires { typename T::Program; }
struct DeclLiteral : SSAName {
    T::Program* prog;
    DeclLiteral(SSAName value, T* decl)
        : SSAName(value), prog(decl->program()) { }
    T::Program* operator->() const { return prog; }
};

struct StmtResult { };
struct Generator : private NodeStreamVisitor<Generator, StmtResult, OwnedValue> {
    friend NodeStreamVisitor<Generator, StmtResult, OwnedValue>;
    Generator(SemanticContext* sema, DeclProgram* program, InstructionStream* targetStream, Decl* containingScope)
        : sema(sema)
        , literalTable(&program->literalTable)
        , constantStream(&program->constantStream)
        , runtimeStream(&program->runtimeStream)
        , targetStream(targetStream)
        , containingScope(containingScope) { }

    SemanticContext* sema = nullptr;

    ConstantTable* literalTable = nullptr;
    InstructionStream* constantStream = nullptr;
    InstructionStream* runtimeStream = nullptr;
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
    StmtResult visitExpressionStmt(ExpressionStmt&, OwnedValue v) {
        if (v.category() == ValueCategory::RValue)
            deallocateRValue(std::move(v));
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
            Type paramType = literalFold(fnDecl, param.type);
            matchTypes(paramType, rawArg.type());

            SSAName argumentName;
            switch (param.model) {
            case ParameterModel::Let:
                requireValueType(rawArg.type());
                [[fallthrough]];
            case ParameterModel::Var: {
                OwnedValue temporary = makeRValue(std::move(rawArg));
                argumentName = temporary.primary();
                temporaryAllocations.push_back(std::move(temporary));
                break;
            }
            case ParameterModel::In: {
                switch (rawArg.category()) {
                case ValueCategory::PValue: {
                    OwnedValue temporary = makeRValue(std::move(rawArg));
                    argumentName = temporary.primary();
                    temporaryAllocations.push_back(std::move(temporary));
                    break;
                }
                case ValueCategory::LValue: {
                    argumentName = rawArg.primary();
                    break;
                }
                case ValueCategory::RValue: {
                    requireValueType(rawArg.type());
                    argumentName = rawArg.primary();
                    temporaryAllocations.push_back(std::move(rawArg));
                    break;
                }
                default:
                    VERIFY_NOT_REACHED();
                }
                break;
            }
            case ParameterModel::InOut:
            case ParameterModel::Out: {
                switch (rawArg.category()) {
                case ValueCategory::LValue: {
                    argumentName = rawArg.primary();
                    break;
                }
                case ValueCategory::PValue:
                case ValueCategory::RValue: {
                    // TODO: Handle error
                    VERIFY_NOT_REACHED();
                }
                default:
                    VERIFY_NOT_REACHED();
                }
                break;
            }
            default:
                VERIFY_NOT_REACHED();
            }
            arguments.push_back(argumentName);
        }
        Type returnType = (Type)literalFold(fnDecl, fnDecl->returnType);

        VERIFY(arguments.size() == fnDecl->parameters.size());
        for (int_t i = 0; i < (int_t)arguments.size(); i++)
            emit<Opcode::Nop>(arguments[i]);
        OwnedValue returnValue = allocateRValue(returnType);
        emit<Opcode::Call>(base.asPureValue(), arguments.size());

        for (OwnedValue& tmp : temporaryAllocations) {
            deallocateRValue(std::move(tmp));
        }
        return returnValue;
    }
    OwnedValue visitParenthesizedExpr(ParenthesizedExpr&) {
        VERIFY_NOT_REACHED();
    }
    OwnedValue visitAccessExpr(AccessExpr&, OwnedValue) {
        VERIFY_NOT_REACHED();
    }

    OwnedValue visitIdentifierExpr(IdentifierExpr& e) {
        auto emitStaticDecl = [&](StaticDecl* decl) -> OwnedValue {
            // For non-template StaticVariableDecls this is the l- or p-value for the variable.
            // In all other cases this is a literal of appropriate type.
            if (auto parameterizedDecl = dyn_cast<ParameterizedDecl>(decl)) {
                if (parameterizedDecl->parameterDecls()->templateParameterCount > 0) {
                    // TODO: Handle templates
                    VERIFY_NOT_REACHED();
                }
                if (isDeclType<TypeDecl>(decl->kind())) {
                    SSAName declLit = literalTable->emit(decl);
                    return PureValue { declLit, typeTypeLiteral() };
                } else if (auto varDeclPtr = dyn_cast<StaticVariableDecl>(decl)) {
                    auto varDecl = useDeclLiteral<StaticVariableDecl>(literalTable->emit(varDeclPtr.value()));
                    return literalFold(varDecl, varDecl->value);
                } else if (auto fnDecl = dyn_cast<FunctionDecl>(decl)) {
                    SSAName declLit = literalTable->emit(fnDecl.value());
                    return PureValue { declLit, functionLiteralTypeLiteral() };
                }
            }
            VERIFY_NOT_REACHED();
        };
        auto emitParameter = [&](ParameterizedDecl* declaringDecl, Word name) -> std::optional<OwnedValue> {
            auto declaringProgram = declaringDecl->program();
            for (auto param : declaringProgram->parameters) {
                if (param.name == name) {
                    VERIFY(targetStream->stream_phase == ValuePhase::Runtime);
                    VERIFY(targetStream == &declaringProgram->runtimeStream);
                    return Value::lvalue((SSAName)param.slot, (Type)param.type);
                }
            }
            return {};
        };
        Decl* d = containingScope;
        Word name = e.identifier();
        for (;;) {
            VERIFY(d != nullptr);
            switch (d->kind()) {

            case DeclKind::Namespace: {
                NamespaceDecl* decl = (NamespaceDecl*)d;
                auto staticDecls = decl->staticDecls()->decls(name);
                if (!staticDecls.empty())
                    return emitStaticDecl(*staticDecls.begin());
                d = decl->declaringDecl();
                continue;
            }

            case DeclKind::StructType:
            case DeclKind::ObjectType: {
                TypeDecl* decl = (TypeDecl*)d;
                auto parameter = emitParameter(decl, name);
                if (parameter.has_value())
                    return std::move(parameter.value());
                auto staticDecls = decl->staticDecls()->decls(name);
                if (!staticDecls.empty())
                    return emitStaticDecl(*staticDecls.begin());
                d = decl->declaringDecl();
                continue;
            }

            case DeclKind::Function:
            case DeclKind::StaticLetVariable:
            case DeclKind::StaticVarVariable: {
                ParameterizedDecl* decl = dyn_cast<ParameterizedDecl>(d);
                auto parameter = emitParameter(decl, name);
                if (parameter.has_value())
                    return std::move(parameter.value());
                d = ((StaticDecl*)d)->declaringDecl();
                continue;
            }

            default:
                VERIFY_NOT_REACHED();
            }
        }
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
    template<typename T>
    SSAName literalFold(DeclLiteral<T> decl, ConstantStreamOperand constant) {
        if (constant.phase() == ValuePhase::Literal)
            return literalTable->emit(decl->literalTable.get((SSAName)constant));
        return constantStream->emit<Opcode::ForeignConstant>(decl, constant);
    }
    template<typename T>
    Type literalFold(DeclLiteral<T> decl, ConstantStreamTypeOperand constant) {
        return (Type)literalFold(decl, (ConstantStreamOperand)constant);
    }
    template<typename T>
    Value literalFold(DeclLiteral<T> decl, ConstantStreamValue value) {
        return { value.category, literalFold(decl, value.primary), literalFold(decl, value.type) };
    }

    Type implicitToType(OwnedValue in) {
        if (literallyEqual(in.type(), &typeType)) {
            return (Type)purify(std::move(in));
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

    PureValue purify(OwnedValue in) {
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
    void bindRValueToSlot(SSAName slot, OwnedValue value) {
        VERIFY(value.category() == ValueCategory::RValue);
        auto& inst = definingInstruction(value.primary());
        VERIFY(inst.opcode() == Opcode::Allocate);
        inst.setOp(Opcode::Nop);
        inst.setB(slot.localize(value.primary().phase()));
        value.releaseValue();
    }

    OwnedValue makeRValue(OwnedValue in) {
        switch (in.category()) {
        case ValueCategory::PValue: {
            OwnedValue out = allocateRValue(in.type());
            emit<Opcode::Store>(out.primary(), in.asPureValue());
            return out;
        }
        case ValueCategory::LValue: {
            // TODO: Implement copying object types.
            requireValueType(in.type());
            return makeRValue(purify(std::move(in)));
        }
        case ValueCategory::RValue: {
            return in;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    template<std::derived_from<Decl> T>
    DeclLiteral<T> useDeclLiteral(SSAName lit) {
        VERIFY(lit.phase() == ValuePhase::Literal);
        Decl* decl = literalTable->get(lit).asDecl();
        sema->requireSignature(decl);
        return { lit, dyn_cast<T>(decl) };
    }

    void requireValueType(Type) { }

    void matchTypes(Type target, Type source) {
        VERIFY(target.phase() == ValuePhase::Literal && source.phase() == ValuePhase::Literal);
        VERIFY(compareConstantsOfSameType(literalTable->get(target), literalTable->get(source)));
    }

    Instruction& definingInstruction(SSAName value) {
        InstructionStream* stream;
        if (value.phase() == ValuePhase::Constant)
            stream = constantStream;
        else if (value.phase() == ValuePhase::Runtime)
            stream = runtimeStream;
        else
            VERIFY_NOT_REACHED();
        return stream->stream[stream->definitions[value.id()]];
    }
};

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
    auto& p = *d.program();
    std::construct_at(&p);
    d.setStatus(DeclStatus::SignatureCheckInProgress);

    SSAName returnSlot;
    if (d.kind() == DeclKind::StaticVarVariable)
        returnSlot = p.constantStream.emit<Opcode::ReturnSlot>();

    Generator g(this, &p, &p.constantStream, &d);
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
    }

    if (d.kind() == DeclKind::StaticLetVariable) {
        p.value = ((Value)g.purify(std::move(initExpr))).localizeConstant();
    } else if (d.kind() == DeclKind::StaticVarVariable) {
        Type type = initExpr.type();
        g.bindRValueToSlot(returnSlot, g.makeRValue(std::move(initExpr)));
        p.value = Value::lvalue(returnSlot, type).localizeConstant();
    } else
        VERIFY_NOT_REACHED();

    d.setStatus(DeclStatus::SignatureChecked);
}

void SemanticContext::signatureCheckTypeDecl(TypeDecl& d) {
    VERIFY(d.status() == DeclStatus::Unchecked);
    auto& p = *d.program();
    std::construct_at(&p);
    d.setStatus(DeclStatus::SignatureCheckInProgress);

    d.setStatus(DeclStatus::SignatureChecked);
}

static ParameterModel kindToModel(DeclKind kind) {
    switch (kind) {
    case DeclKind::LetParameter:
        return ParameterModel::Let;
    case DeclKind::VarParameter:
        return ParameterModel::Var;
    case DeclKind::InParameter:
        return ParameterModel::In;
    case DeclKind::InOutParameter:
        return ParameterModel::InOut;
    case DeclKind::OutParameter:
        return ParameterModel::Out;
    default:
        VERIFY_NOT_REACHED();
    }
}
void SemanticContext::signatureCheckFunctionDecl(FunctionDecl& d) {
    VERIFY(d.status() == DeclStatus::Unchecked);
    auto& p = *d.program();
    std::construct_at(&p);
    d.setStatus(DeclStatus::SignatureCheckInProgress);

    Generator g(this, &p, &p.constantStream, &d);

    auto parameterDecls = d.parameterDecls()->parameters();
    VERIFY(d.parameterDecls()->templateParameterCount == 0);
    p.parameters.reserve(parameterDecls.size());
    for (ParameterDecl* param : parameterDecls) {
        auto type = g.visitExpression(param->typeExpr())
                        .transform([&](OwnedValue type) { return g.implicitToType(std::move(type)); })
                        .value();
        SSAName slot = p.runtimeStream.emit<Opcode::ParameterSlot>();
        p.parameters.push_back({
            param->name,
            kindToModel(param->kind()),
            type.localizeConstant(),
            slot.localizeRuntime(),
        });
    }
    auto returnType = g.visitExpression(d.returnTypeExpr())
                          .transform([&](OwnedValue type) { return g.implicitToType(std::move(type)); })
                          .or_else([&] -> std::optional<Type> { return (Type)p.literalTable.emit(&voidType); })
                          .value();
    p.returnType = returnType.localizeConstant();
    p.returnSlot = p.runtimeStream.emit<Opcode::ReturnSlot>().localizeRuntime();

    d.setStatus(DeclStatus::SignatureChecked);
}
void SemanticContext::checkBody(FunctionDecl& d) {
    requireSignature(&d);
    auto& p = *d.program();

    Generator g(this, &p, &p.runtimeStream, &d);
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
            dumpIR(child, *wordTable);
        }
    }
}