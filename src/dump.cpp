#include "Parser.h"
#include <iostream>
#include <vector>

namespace {

enum class IsLastChild : bool {
    No = false,
    Yes = true,
};

struct Name {
    std::string_view name = {};
    bool withdot = false;

    Name withName(std::string_view name, bool withdot) {
        return { name, withdot };
    }
};

struct STDumper : STContext {
    std::ostream& out;
    std::vector<char> prefix = {};

    template<typename T> // T = { Stmt, Decl }
    void dump(Ptr<T> e, Name name) {
        if (name.name.length() > 0) {
            if (name.withdot)
                out << '.';
            out << name.name << " = ";
        }
        out << toString(at(e).kind) << ' ';
        dispatchVisit(e);
        out << '\n';
        dispatchChildren(e);
    }

    template<typename T> // T = { Stmt, Decl }
    void child(Ptr<T> e, IsLastChild last) {
        out << std::string_view { prefix.data(), prefix.size() };
        if ((bool)last) {
            out << "'-";
            prefix.push_back(' ');
        } else {
            out << "|-";
            prefix.push_back('|');
        }
        prefix.push_back(' ');

        dump(e, {});

        prefix.resize(prefix.size() - 2);
    }
    void child(Arguments args, IsLastChild last) {
        for (uint32_t i = 0; i < args.args.count; i++) {
            auto arg = at(args.args, i);
            child(arg.source, (IsLastChild)((bool)last && i == args.args.count - 1));
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
    void visitAssignStmt(Ptr<AssignStmt> e) {
        out << '\'' << toShortString(at(e).op) << '\'';
    }
    void visitNullStmt(Ptr<NullStmt>) { }
    void visitCompoundStmt(Ptr<CompoundStmt>) { }
    void visitIntLiteralExpr(Ptr<IntLiteralExpr> e) {
        out << '\'' << at(e).value << '\'';
    }
    void visitLetStmt(Ptr<LetStmt>) { }

    void visitStructDecl(Ptr<StructDecl>) { }
    void visitVarDecl(Ptr<VarDecl> e) {
        if (at(e).qual == VarDecl::Qualifier::Const)
            out << "const ";
        else if (at(e).qual == VarDecl::Qualifier::Mut)
            out << "mut ";
        out << '\'' << sview(at(e).name) << '\'';
    }
    void visitFnDecl(Ptr<FnDecl>) { }

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
        child(at(e).base, IsLastChild::Yes);
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
    void childrenIdentifierExpr(Ptr<IdentifierExpr>) { }
    void childrenBinaryOperatorExpr(Ptr<BinaryOperatorExpr> e) {
        child(at(e).left, IsLastChild::No);
        child(at(e).right, IsLastChild::Yes);
    }
    void childrenAssignStmt(Ptr<AssignStmt> e) {
        child(at(e).left, IsLastChild::No);
        child(at(e).right, IsLastChild::Yes);
    }
    void childrenNullStmt(Ptr<NullStmt>) { }
    void childrenCompoundStmt(Ptr<CompoundStmt> e) {
        childrenSpan(at(e).body, IsLastChild::Yes);
    }
    void childrenIntLiteralExpr(Ptr<IntLiteralExpr>) { }
    void childrenLetStmt(Ptr<LetStmt> e) {
        child(at(e).decl, IsLastChild::Yes);
    }

    void childrenStructDecl(Ptr<StructDecl> e) {
        childrenSpan(at(e).decls, IsLastChild::Yes);
    }
    void childrenVarDecl(Ptr<VarDecl> e) {
        if (at(e).type)
            child(at(e).type, (IsLastChild)(bool)(at(e).initializer));
        if (at(e).initializer)
            child(at(e).initializer, IsLastChild::Yes);
    }
    void childrenFnDecl(Ptr<FnDecl> e) {
        child(at(e).body, IsLastChild::Yes);
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

void dump(STContext context, Ptr<Stmt> e) {
    STDumper dumper { context, std::cout };
    dumper.dump(e, {});
}
void dump(STContext context, Ptr<Decl> e) {
    STDumper dumper { context, std::cout };
    dumper.dump(e, {});
}