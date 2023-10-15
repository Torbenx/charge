#include "semantic.h"
#include "NodeStreamVisitor.h"
#include <ranges>

struct DeclResult { };
struct StmtResult { };

// For 'dereference(inout self) <=> *stored_id;' we need to effectively evalute the dereference
// of 'stored_id' at the call site.

struct Generator : NodeStreamVisitor<Generator, DeclResult, StmtResult, ExprValue> {
    using NodeStreamVisitor::NodeStreamVisitor;
    SemanticContext* sema = nullptr;

    SSAName typeType;
    std::vector<LookupContext> lookupStack;

    // declarations
    DeclResult visitStaticDecl(StaticDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitFunctionDecl(FunctionDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitStaticVariableDecl(StaticVariableDecl&) { VERIFY_NOT_REACHED(); }
    DeclResult visitParameterOrMemberDecl(ParameterOrMemberDecl&) { VERIFY_NOT_REACHED(); }

    // statements
    StmtResult visitExpressionStmt(ExpressionStmt&, ExprValue expr) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitUpdateStmt(UpdateStmt& e, ExprValue base) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitLetStmt(LetStmt&) { VERIFY_NOT_REACHED(); }
    StmtResult visitCompoundStmt(CompoundStmt& e) {
        VERIFY_NOT_REACHED();
    }
    StmtResult visitIfStmt(IfStmt& e, ExprValue condition) {
        VERIFY_NOT_REACHED();
    }

    // expressions
    ExprValue visitUnaryOperatorExpr(UnaryOperatorExpr& e, ExprValue subExpr) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitBinaryOperatorExpr(BinaryOperatorExpr& e, ExprValue left) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitCallExpr(CallExpr& e, ExprValue base) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitParenthesizedExpr(ParenthesizedExpr& e) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitAccessExpr(AccessExpr& e, ExprValue base) {
        VERIFY_NOT_REACHED();
    }
    std::optional<id<Decl>> performLookupInStack(Word id, LocalSourceRange idLoc) {
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
        id<Decl> decl = maybeDecl.value();
        // The value of an IdentifierExpr:
        //  - For a variable-decl the hypothetical value of the load.
        //  - For a type-decl a literal for the type.
        //  - For a function-decl a literal for the function.
        //  - For a templated-decl a literal for the template.
    }
    ExprValue visitCompoundExpr(CompoundExpr& e) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitIfExpr(IfExpr& e, ExprValue cond) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitCommaElseExpr(CommaElseExpr& e, ExprValue base) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitNumericLiteralExpr(NumericLiteralExpr&) { VERIFY_NOT_REACHED(); }
    ExprValue visitCharacterLiteralExpr(CharacterLiteralExpr&) { VERIFY_NOT_REACHED(); }
    ExprValue visitDesignateArgument(DesignateArgument& e, ExprValue argument) {
        VERIFY_NOT_REACHED();
    }
    ExprValue visitParameterize(Parameterize&, ExprValue base) {
        VERIFY_NOT_REACHED();
    }

    // emit
    ExprValue emitDeclarationReference(id<Decl> decl) {
        emit<DeclarationReference>({});
    }
    template<std::derived_from<Inst> I>
    void emit(I inst) {
    }
};

std::optional<id<Decl>> SemanticContext::performLookup(LookupContext& ctx, Word id, LocalSourceRange idLoc) {
    return {};
}

void test(Node* node) {
    Generator g(node);
    g.visitGeneric(ExpressionPrecedence::Statement);
}