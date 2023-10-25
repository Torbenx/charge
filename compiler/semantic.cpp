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
    Generator(SemanticContext* sema, ConstantTable* literalTable, InstructionStream* constantStream, InstructionStream* targetStream)
        : sema(sema), literalTable(literalTable), constantStream(constantStream), targetStream(targetStream) { }

    SemanticContext* sema = nullptr;

    ConstantTable* literalTable = nullptr;
    InstructionStream* constantStream = nullptr;
    InstructionStream* targetStream = nullptr;
    std::vector<LookupContext> lookupStack;

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
        VERIFY_NOT_REACHED();
    }
    ExprValue visitParenthesizedExpr(ParenthesizedExpr&) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitAccessExpr(AccessExpr&, ExprValue) {
        VERIFY_NOT_REACHED();
    }
    std::optional<Decl*> performLookupInStack(Word id, LocalSourceRange idLoc) {
        for (LookupContext& ctx : std::ranges::views::reverse(lookupStack)) {
            auto result = sema->performLookup(ctx, id, idLoc);
            if (result.has_value()) {
                return result.value();
            }
        }
        return {};
    }
    ExprValue visitIdentifierExpr(IdentifierExpr& e) {
        auto maybeDecl = performLookupInStack(e.identifierWord(), e.identifierLocation());
        if (!maybeDecl.has_value())
            return sema->errorHandler->unresolvedIdentifier();
        Decl* decl = maybeDecl.value();
        // The value of an IdentifierExpr:
        //  - For a templated-decl a literal for the template.
        //  - For a variable-decl the hypothetical value of the load.
        //  - For a type-decl a literal for the type.
        //  - For a function-decl a literal for the function.

        if (auto* staticDecl = dyn_cast<StaticDecl>(decl)) {
            if (staticDecl->kind() == DeclKind::StructType || staticDecl->kind() == DeclKind::ObjectType) {
                SSAName declLit = literalTable->emit(staticDecl);
                return { declLit, typeTypeLiteral() };
            } else if (auto* varDecl = dyn_cast<StaticVariableDecl>(decl)) {
                SSAName varDeclLit = literalTable->emit(varDecl);
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
            if constexpr (std::is_same_v<decltype(val), CallValue>)
                VERIFY_NOT_REACHED();
        }, in.value);
    }
    TypedValue materialize(const ExprValue& in) { return materializeIn(targetStream, in); }
    TypedValue materializeConstant(const ExprValue& in) { return materializeIn(constantStream, in); }
};

std::optional<id<Decl>> SemanticContext::performLookup(LookupContext&, Word, LocalSourceRange) {
    return {};
}

void SemanticContext::signatureCheckStaticVariableDecl(StaticVariableDecl& d) {
    Generator g(this, &d.program.literalTable, &d.program.constantStream, &d.program.constantStream);
    auto typeExpr = g.visitExpr(d.typeExpr());
    auto typeValue = g.implicitToType(typeExpr.value());
    VERIFY(g.literallyEqual(typeValue.type, &typeType));
    d.typeValue = d.program.toConstantOperand(typeValue.value);
}