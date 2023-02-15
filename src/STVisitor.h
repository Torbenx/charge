#pragma once

#include "statement.h"

enum class IsLastChild : bool {
    No = false,
    Yes = true,
};

template<typename Impl, typename... Args>
struct STChildren {
    Impl* impl() { return static_cast<Impl*>(this); }
    template<typename T>
    T& Iat(Ptr<T> p) { return impl()->at(p); }

    void childrenUnaryOperatorExpr(Ptr<UnaryOperatorExpr> e, Args... args) {
        impl()->child(Iat(e).subExpr, IsLastChild::Yes, args...);
    }
    void childrenParenExpr(Ptr<ParenExpr> e, Args... args) {
        impl()->child(Iat(e).subExpr, IsLastChild::Yes, args...);
    }
    void childrenAccessExpr(Ptr<AccessExpr> e, Args... args) {
        impl()->child(Iat(e).base, IsLastChild::Yes, args...);
    }
    void childrenImmediateBraceExpr(Ptr<ImmediateBraceExpr> e, Args... args) {
        impl()->child(Iat(e).args, IsLastChild::Yes, args...);
    }
    void childrenCallExpr(Ptr<CallExpr> e, Args... args) {
        if (Iat(e).args.args.count == 0) {
            impl()->child(Iat(e).base, IsLastChild::Yes, args...);
            impl()->child(Iat(e).args, IsLastChild::Yes, args...);
        } else {
            impl()->child(Iat(e).base, IsLastChild::No, args...);
            impl()->child(Iat(e).args, IsLastChild::Yes, args...);
        }
    }
    void childrenIdentifierExpr(Ptr<IdentifierExpr> e, Args... args) {
        impl()->child(Iat(e).identifier.args, IsLastChild::Yes, args...);
    }
    void childrenBinaryOperatorExpr(Ptr<BinaryOperatorExpr> e, Args... args) {
        impl()->child(Iat(e).left, IsLastChild::No, args...);
        impl()->child(Iat(e).right, IsLastChild::Yes, args...);
    }
    void childrenAssignStmt(Ptr<AssignStmt> e, Args... args) {
        impl()->child(Iat(e).left, IsLastChild::No, args...);
        impl()->child(Iat(e).right, IsLastChild::Yes, args...);
    }
    void childrenNullStmt(Ptr<NullStmt>, Args...) { }
    void childrenCompoundStmt(Ptr<CompoundStmt> p, Args... args) {
        CompoundStmt& e = Iat(p);
        impl()->child((Ptr<DeclContext>)p, (IsLastChild)(e.body.count == 0), args...);
        for (uint32_t i = 0; i < e.body.count; i++) {
            impl()->child(Iat(e.body[i]), (IsLastChild)(i == e.body.count - 1), args...);
        }
    }
    void childrenIntLiteralExpr(Ptr<IntLiteralExpr>, Args...) { }
    void childrenLetStmt(Ptr<LetStmt> e, Args... args) {
        impl()->child(Iat(e).decl, IsLastChild::Yes, args...);
    }

    void childrenStructDecl(Ptr<StructDecl>, Args...) { }
    void childrenVarDecl(Ptr<VarDecl> d, Args... args) {
        impl()->child(Iat(d).initializer, IsLastChild::Yes, args...);
    }
    void childrenFnDecl(Ptr<FnDecl>, Args...) { }

    void child(Ptr<Stmt>, IsLastChild, Args...) { }
    void child(Ptr<Decl>, IsLastChild, Args...) { }
    void child(Ptr<DeclContext>, IsLastChild, Args...) { }
    void child(Arguments, IsLastChild, Args...) { }

    void dispatchChildren(Ptr<Stmt> e, Args... args) {
#define STMT_KIND(kind)                                \
    case StmtKind::kind:                               \
        impl()->children##kind((Ptr<kind>)e, args...); \
        break;

        switch (Iat(e).kind) {
            ENUMERATE_STMT_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef STMT_KIND
    }
    void dispatchChildren(Ptr<Decl> d, Args... args) {
#define DECL_KIND(kind)                                \
    case DeclKind::kind:                               \
        impl()->children##kind((Ptr<kind>)d, args...); \
        break;

        switch (Iat(d).kind) {
            ENUMERATE_DECL_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef DECL_KIND
    }
};

template<typename Impl, typename... Args>
struct STVisitor {
    Impl* impl() { return static_cast<Impl*>(this); }
    template<typename T>
    T& Iat(Ptr<T> p) { return impl()->at(p); }

#define STMT_KIND(kind) \
    void visit##kind(Ptr<kind>, Args...) { }
    ENUMERATE_STMT_KINDS
#undef STMT_KIND

    void dispatchVisit(Ptr<Stmt> e, Args... args) {
#define STMT_KIND(kind)                             \
    case StmtKind::kind:                            \
        impl()->visit##kind((Ptr<kind>)e, args...); \
        break;

        switch (Iat(e).kind) {
            ENUMERATE_STMT_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef STMT_KIND
    }

#define DECL_KIND(kind) \
    void visit##kind(Ptr<kind>, Args...) { }
    ENUMERATE_DECL_KINDS
#undef DECL_KIND

    void dispatchVisit(Ptr<Decl> d, Args... args) {
#define DECL_KIND(kind)                                \
    case DeclKind::kind:                               \
        impl()->visit##kind((Ptr<kind>)d, args...); \
        break;

        switch (Iat(d).kind) {
            ENUMERATE_DECL_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef DECL_KIND
    }
};