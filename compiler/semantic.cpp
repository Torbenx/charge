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
    return { stream_phase, id };
}

InstructionOperand InstructionStream::localize(SSAName name) const {
    if (name.phase() == stream_phase)
        return { false, name.id() };
    if (std::to_underlying(name.phase()) + 1 == std::to_underlying(stream_phase))
        return { true, name.id() };
    VERIFY_NOT_REACHED();
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
SSAName InstructionStream::emit_foreign_const(Opcode op, SSAName decl, ConstantStreamInstructionOperand constant) {
    SSAName out = allocateName();
    stream.push_back({ op, localize(out), localize(decl), constant });
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

struct StmtResult { };
struct Generator : NodeStreamVisitor<Generator, StmtResult, ExprValue> {
    Generator(SemanticContext* sema, ConstantTable* literalTable, InstructionStream* constantStream, InstructionStream* targetStream, Decl* containingScope)
        : sema(sema), literalTable(literalTable), constantStream(constantStream), targetStream(targetStream), containingScope(containingScope) { }

    SemanticContext* sema = nullptr;

    ConstantTable* literalTable = nullptr;
    InstructionStream* constantStream = nullptr;
    InstructionStream* targetStream = nullptr;
    Decl* containingScope = nullptr;

    // statements
    StmtResult visitExpressionStmt(ExpressionStmt&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitUpdateStmt(UpdateStmt&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitLetStmt(LetStmt&) { VERIFY_NOT_REACHED(); }
    StmtResult visitCompoundStmt(CompoundStmt&) {
        VERIFY_NOT_REACHED();
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
    ExprValue visitCallExpr(CallExpr&, ExprValue) {
        // Because of overload resolution we don't know which implicit conversions to apply after
        // looking at just a subset of arguments. There are two kinds of solutions here:
        //  1. Don't allow implicit conversions and overload resolution at same time. So we either
        //     perfrom implicit conversions on an not overloaded set OR call an overload without
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
        VERIFY_NOT_REACHED();
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

        if (auto* parameterizedDecl = dyn_cast<ParameterizedDecl>(decl)) {
            if (parameterizedDecl->parameterDecls()->templateParameterCount > 0) {
                // TODO: Handle templates
                VERIFY_NOT_REACHED();
            }
            if (isDeclType<TypeDecl>(decl->kind())) {
                SSAName declLit = literalTable->emit(decl);
                return { declLit, typeTypeLiteral() };
            } else if (auto* varDecl = dyn_cast<StaticVariableDecl>(decl)) {
                SSAName varDeclLit = literalTable->emit(varDecl);
                sema->requireSignature(varDecl);
                SSAName type;
                if (varDecl->typeValue.phase() == ValuePhase::Literal) {
                    // literal fold
                    type = literalTable->emit(varDecl->program.literalTable.get(varDecl->typeValue.id()));
                } else {
                    type = constantStream->emit<Opcode::ForeignConstant>(varDeclLit, varDecl->typeValue);
                }
                SSAName value = constantStream->emit<Opcode::StaticVariableId>(varDeclLit);
                return { LoadValue { value }, type };
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

    //
    SSAName typeTypeLiteral() {
        return literalTable->emit(&typeType);
    }
    TypedValue implicitToType(const ExprValue& in) {
        if (literallyEqual(in.type, &typeType)) {
            return materialize(in);
        }
        VERIFY_NOT_REACHED();
    }
    bool literallyEqual(SSAName leftName, TypedConstant right) {
        if (leftName.phase() != ValuePhase::Literal)
            return false;
        TypedConstant left = literalTable->get(leftName.id());
        return compareConstantsOfSameType(left, right);
    }
    TypedValue materializeIn(InstructionStream* stream, const ExprValue& in) {
        return std::visit([&](auto val) -> TypedValue {
            if constexpr (std::is_same_v<decltype(val), SSAName>)
                return { val, in.type };
            if constexpr (std::is_same_v<decltype(val), LoadValue>)
                return { stream->emit<Opcode::Load>(val.substance), in.type };
        },
            in.value);
    }
    TypedValue materialize(const ExprValue& in) { return materializeIn(targetStream, in); }
    TypedValue materializeConstant(const ExprValue& in) { return materializeIn(constantStream, in); }
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
            ParameterizedDecl* decl = (ParameterizedDecl*)d;
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
    VERIFY(d->status() != DeclStatus::SignatureCheckInProgress);
    if (auto* decl = dyn_cast<TypeDecl>(d)) {
        signatureCheckTypeDecl(*decl);
    } else if (auto* decl = dyn_cast<FunctionDecl>(d)) {
        signatureCheckFunctionDecl(*decl);
    } else if (auto* decl = dyn_cast<StaticVariableDecl>(d)) {
        signatureCheckStaticVariableDecl(*decl);
    } else {
        VERIFY_NOT_REACHED();
    }
}

void SemanticContext::signatureCheckStaticVariableDecl(StaticVariableDecl& d) {
    VERIFY(d.status() == DeclStatus::Unchecked);
    d.setStatus(DeclStatus::SignatureCheckInProgress);

    Generator g(this, &d.program.literalTable, &d.program.constantStream, &d.program.constantStream, &d);
    auto typeExpr = g.visitExpr(d.typeExpr());
    auto typeValue = g.implicitToType(typeExpr.value());
    VERIFY(g.literallyEqual(typeValue.type, &typeType));
    d.typeValue = d.program.toConstantOperand(typeValue.value);

    d.setStatus(DeclStatus::SignatureChecked);
}

void SemanticContext::signatureCheckTypeDecl(TypeDecl& d) {
    VERIFY(d.status() == DeclStatus::Unchecked);
    d.setStatus(DeclStatus::SignatureCheckInProgress);

    d.setStatus(DeclStatus::SignatureChecked);
}
void SemanticContext::signatureCheckFunctionDecl(FunctionDecl&) { VERIFY_NOT_REACHED(); }

void SemanticContext::check(NamespaceDecl* parent) {
    for (Decl* child : *parent->staticDecls()) {
        if (auto* decl = dyn_cast<NamespaceDecl>(child)) {
            check(decl);
        } else {
            requireSignature(child);
        }
    }
}