#include "Parser.h"
#include <iostream>
#include <vector>

namespace {

enum class IsLastChild : bool {
    No = false,
    Yes = true,
};

struct STDumper : STContext {
    std::ostream& out;
    std::vector<char> prefix = {};

    template<typename T> // T = { Stmt, Expr, Decl }
    void dump(Ptr<T> e, std::string_view name = {}) {
        auto oldPrefixSize = prefix.size();
        if (name.length() > 0) {
            out << name << ": ";
            for (uint32_t i = 0; i < name.length() + 2; i++)
                prefix.push_back(' ');
        }

        out << toString(at(e).kind) << ' ';
        dispatchVisit(e);
        out << '\n';

        dispatchChildren(e);
        prefix.resize(oldPrefixSize);
    }

    template<typename T> // T = { Stmt, Expr, Decl }
    void child(Ptr<T> e, IsLastChild last, std::string_view name = {}) {
        out << std::string_view { prefix.data(), prefix.size() };
        if ((bool)last) {
            out << "'-";
            prefix.push_back(' ');
        } else {
            out << "|-";
            prefix.push_back('|');
        }
        prefix.push_back(' ');

        dump(e, name);

        prefix.resize(prefix.size() - 2);
    }
    void child(Arguments args, IsLastChild last) {
        for (uint32_t i = 0; i < args.args.count; i++) {
            auto arg = at(args.args, i);
            child(arg.source, (IsLastChild)((bool)last && i == args.args.count - 1), { sview(arg.target) });
        }
    }

    void visitUnaryOperatorExpr(Ptr<UnaryOperatorExpr> e) {
        out << '\'' << toShortString(at(e).op) << '\'';
    }
    void visitParenExpr(Ptr<ParenExpr>) { }
    void visitAccessExpr(Ptr<AccessExpr> e) {
        out << '\'' << (at(e).isStatic ? "::" : ".") << sview(at(e).member.word) << '\'';
    }
    void visitImmediateBraceExpr(Ptr<ImmediateBraceExpr>) { }
    void visitCallExpr(Ptr<CallExpr> e) {
        out << '\'' << (at(e).callKind == CallKind::Paren ? "()" : "[]") << '\'';
    }
    void visitIdentifierExpr(Ptr<IdentifierExpr> e) {
        out << '\'' << sview(at(e).identifier.word) << '\'';
    }
    void visitBinaryOperatorExpr(Ptr<BinaryOperatorExpr> e) {
        out << '\'' << toShortString(at(e).op) << '\'';
    }
    void visitIntLiteralExpr(Ptr<IntLiteralExpr> e) {
        out << '\'' << at(e).value << '\'';
    }

    void visitAssignStmt(Ptr<AssignStmt> e) {
        out << '\'';
        if (at(e).op.has_value())
            out << toShortString(at(e).op.value());
        out << "='";
    }
    void visitNullStmt(Ptr<NullStmt>) { }
    void visitCompoundStmt(Ptr<CompoundStmt>) { }
    void visitLetStmt(Ptr<LetStmt>) { }
    void visitExprStmt(Ptr<ExprStmt>) { }
    void visitReturnStmt(Ptr<ReturnStmt>) { }
    void visitIfStmt(Ptr<IfStmt>) { }

    void visitStructDecl(Ptr<StructDecl> e) {
        out << '\'' << sview(at(e).name) << '\'';
    }
    void visitLocalDecl(Ptr<LocalDecl> e) {
        if (at(e).isMutable)
            out << "mut ";
        out << '\'' << sview(at(e).name) << '\'';
    }
    void visitGlobalDecl(Ptr<GlobalDecl> e) {
        if (at(e).isMutable)
            out << "mut ";
        out << '\'' << sview(at(e).name) << '\'';
    }
    void visitFnDecl(Ptr<FnDecl> e) {
        out << '\'' << sview(at(e).name) << '\'';
    }
    void visitHasDecl(Ptr<HasDecl>) { }

    template<typename T>
    void childrenSpan(Span<T> s, IsLastChild isLast) {
        for (uint32_t i = 0; i < s.count; i++)
            child(at(s[i]), (IsLastChild)((bool)isLast && i == s.count - 1));
    }
    void childrenUnaryOperatorExpr(Ptr<UnaryOperatorExpr> e) {
        child(at(e).subExpr, IsLastChild::Yes);
    }
    void childrenParenExpr(Ptr<ParenExpr> e) {
        child(at(e).subExpr, IsLastChild::Yes);
    }
    void childrenAccessExpr(Ptr<AccessExpr> e) {
        child(at(e).base, (IsLastChild)(at(e).member.args.count == 0));
        child(at(e).member, IsLastChild::Yes);
    }
    void childrenImmediateBraceExpr(Ptr<ImmediateBraceExpr> e) {
        child(at(e).args, IsLastChild::Yes);
    }
    void childrenCallExpr(Ptr<CallExpr> e) {
        if (at(e).args.args.count == 0) {
            child(at(e).base, IsLastChild::Yes);
            child(at(e).args, IsLastChild::Yes);
        } else {
            child(at(e).base, IsLastChild::No);
            child(at(e).args, IsLastChild::Yes);
        }
    }
    void childrenIdentifierExpr(Ptr<IdentifierExpr> e) {
        child(at(e).identifier, IsLastChild::Yes);
    }
    void childrenBinaryOperatorExpr(Ptr<BinaryOperatorExpr> e) {
        child(at(e).left, IsLastChild::No);
        child(at(e).right, IsLastChild::Yes);
    }
    void childrenIntLiteralExpr(Ptr<IntLiteralExpr>) { }
    void childrenAssignStmt(Ptr<AssignStmt> e) {
        child(at(e).left, IsLastChild::No);
        child(at(e).right, IsLastChild::Yes);
    }

    void childrenNullStmt(Ptr<NullStmt>) { }
    void childrenCompoundStmt(Ptr<CompoundStmt> e) {
        childrenSpan(at(e).body, IsLastChild::Yes);
    }
    void childrenLetStmt(Ptr<LetStmt> e) {
        child(at(e).decl, IsLastChild::Yes);
    }
    void childrenExprStmt(Ptr<ExprStmt> e) {
        child(at(e).expr, IsLastChild::Yes);
    }
    void childrenReturnStmt(Ptr<ReturnStmt> e) {
        child(at(e).expr, IsLastChild::Yes);
    }
    void childrenIfStmt(Ptr<IfStmt> e) {
        child(at(e).condition, IsLastChild::No);
        child(at(e).ifTrue, (IsLastChild) !(bool)at(e).ifFalse);
        if (at(e).ifFalse)
            child(at(e).ifFalse, IsLastChild::Yes);
    }

    void childrenStructDecl(Ptr<StructDecl> e) {
        childrenSpan(at(e).staticDecls, (IsLastChild)(at(e).params.count == 0));
        childrenSpan(at(e).params, IsLastChild::Yes);
    }
    void childrenVarInfo(VarInfo& info) {
        if (info.type)
            child(info.type, (IsLastChild) !(bool)(info.initializer));
        if (info.initializer)
            child(info.initializer, IsLastChild::Yes);
    }
    void childrenLocalDecl(Ptr<LocalDecl> e) {
        childrenVarInfo(at(e));
    }
    void childrenGlobalDecl(Ptr<GlobalDecl> e) {
        childrenVarInfo(at(e));
    }
    void childrenFnDecl(Ptr<FnDecl> e) {
        child(at(e).body, IsLastChild::Yes);
    }
    void childrenHasDecl(Ptr<HasDecl> e) {
        child(at(e).type, (IsLastChild)(at(e).decls.count == 0));
        childrenSpan(at(e).decls, IsLastChild::Yes);
    }

    void dispatchChildren(Ptr<Stmt> e) {
#define STMT_KIND(kind)               \
    case StmtKind::kind:              \
        children##kind((Ptr<kind>)e); \
        break;

        switch (at(e).kind) {
            ENUMERATE_STMT_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef STMT_KIND
    }
    void dispatchChildren(Ptr<Expr> e) {
#define EXPR_KIND(kind)               \
    case ExprKind::kind:              \
        children##kind((Ptr<kind>)e); \
        break;

        switch (at(e).kind) {
            ENUMERATE_EXPR_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef EXPR_KIND
    }
    void dispatchChildren(Ptr<Decl> d) {
#define DECL_KIND(kind)               \
    case DeclKind::kind:              \
        children##kind((Ptr<kind>)d); \
        break;

        switch (at(d).kind) {
            ENUMERATE_DECL_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef DECL_KIND
    }

    void dispatchVisit(Ptr<Stmt> e) {
#define STMT_KIND(kind)            \
    case StmtKind::kind:           \
        visit##kind((Ptr<kind>)e); \
        break;

        switch (at(e).kind) {
            ENUMERATE_STMT_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef STMT_KIND
    }
    void dispatchVisit(Ptr<Expr> e) {
#define EXPR_KIND(kind)            \
    case ExprKind::kind:           \
        visit##kind((Ptr<kind>)e); \
        break;

        switch (at(e).kind) {
            ENUMERATE_EXPR_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef EXPR_KIND
    }
    void dispatchVisit(Ptr<Decl> d) {
#define DECL_KIND(kind)            \
    case DeclKind::kind:           \
        visit##kind((Ptr<kind>)d); \
        break;

        switch (at(d).kind) {
            ENUMERATE_DECL_KINDS
        default:
            VERIFY_NOT_REACHED();
        }

#undef DECL_KIND
    }
};
}

void dump(STContext context, Ptr<Stmt> e, std::string_view name) {
    STDumper dumper { context, std::cout };
    dumper.dump(e, name);
}
void dump(STContext context, Ptr<Expr> e, std::string_view name) {
    STDumper dumper { context, std::cout };
    dumper.dump(e, name);
}
void dump(STContext context, Ptr<Decl> e, std::string_view name) {
    STDumper dumper { context, std::cout };
    dumper.dump(e, name);
}