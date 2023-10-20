#include "semantic.h"
#include "NodeStreamVisitor.h"
#include <ranges>

struct DeclResult { };
struct StmtResult { };

// For 'dereference(inout self) <=> *stored_id;' we need to effectively evaluate the dereference
// of 'stored_id' at the call site.

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

SSAName ValueTable::emit(uint64_t val) {
    size_t id = values.size();
    values.push_back(val);
    return { table_phase, id };
}

SSAName ValueTable::emit(Decl* decl) {
    return emit((uintptr_t)decl);
}

static TypeDecl typeType { NodeKind::StructTypeDecl, { words["type"], SingleTokenSourceRange() }, {} };

struct Generator : NodeStreamVisitor<Generator, DeclResult, StmtResult, ExprValue> {
    Generator(SemanticContext* sema, ValueTable* literalTable, InstructionStream* constantStream, InstructionStream* targetStream)
        : sema(sema), literalTable(literalTable), constantStream(constantStream), targetStream(targetStream) { }

    SemanticContext* sema = nullptr;

    ValueTable* literalTable = nullptr;
    InstructionStream* constantStream = nullptr;
    InstructionStream* targetStream = nullptr;
    std::vector<LookupContext> lookupStack;

    // declarations
    DeclResult visitModuleDecl(ModuleDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitNamespaceDecl(NamespaceDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitTypeDecl(TypeDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitFunctionDecl(FunctionDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitStaticVariableDecl(StaticVariableDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitParameterOrMemberDecl(ParameterOrMemberDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitHasMemberDecl(HasMemberDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitBlockLetDecl(BlockLetDecl&) { VERIFY_NOT_REACHED(); }

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

        if (isNodeType<StaticDecl>(decl->kind())) {
            StaticDecl* staticDecl = (StaticDecl*)decl;
            if (staticDecl->decls().templateParamters().size() > 0) {
                // TODO: Handle templates
                VERIFY_NOT_REACHED();
            }
            if (isNodeType<TypeDecl>(staticDecl->kind())) {
                SSAName typeLit = literalTable->emit(&typeType);
                SSAName declLit = literalTable->emit(staticDecl);
                return { LiteralValue { declLit }, typeLit };
            } else if (isNodeType<StaticVariableDecl>(staticDecl->kind())) {
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
    ExprValue implicitToType(ExprValue in) {
        if (literallyEqual(in.type, &typeType)) {
            return in;
        }
        VERIFY_NOT_REACHED();
    }
    bool literallyEqual(SSAName l, Decl* r) {
        return l.phase() == ValuePhase::Literal && (Decl*)literalTable->values[l.id()] == r;
    }
};

std::optional<id<Decl>> SemanticContext::performLookup(LookupContext& ctx, Word id, LocalSourceRange idLoc) {
    return {};
}

void SemanticContext::signatureCheckStaticVariableDecl(StaticVariableDecl& d) {
    Generator g(this, &d.program.literalTable, &d.program.constantStream, &d.program.constantStream);
    auto typeExpr = g.visitExpr(d.typeExpr());
    g.implicitToType(typeExpr.value());
}