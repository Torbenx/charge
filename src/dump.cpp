#include "Parser.h"
#include "STVisitor.h"
#include <iostream>
#include <vector>

namespace {
struct Name {
    std::string_view name = {};
    bool withdot = false;

    Name withName(std::string_view name, bool withdot) {
        return { name, withdot };
    }
};
struct STDumper : STContext, STChildren<STDumper, Name>, STVisitor<STDumper> {
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
        dispatchChildren(e, {});
    }

    template<typename T> // T = { Stmt, Decl }
    void child(Ptr<T> e, IsLastChild last, Name name) {
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
    void child(Arguments args, IsLastChild last, Name) {
        for (uint32_t i = 0; i < args.args.count; i++) {
            auto arg = at(args.args, i);
            child(arg.source, (IsLastChild)((bool)last && i == args.args.count - 1), { sview(arg.target), true });
        }
    }

    void visitUnaryOperatorExpr(Ptr<UnaryOperatorExpr> e) {
        out << '\'' << toShortString(at(e).op) << '\'';
    }
    void visitAccessExpr(Ptr<AccessExpr> e) {
        out << "'." << sview(at(e).member) << '\'';
    }
    void visitCallExpr(Ptr<CallExpr> e) {
        out << '\'' << (at(e).callKind == CallKind::Paren ? "()" : "[]") << '\'';
    }
    void printIdentifier(const ParametricIdentifier& ident) {
        out << '\'';
        if (ident.global)
            out << "::";
        out << sview(at(ident.elements, 0));
        for (uint32_t i = 1; i < ident.elements.count; i++)
            out << "::" << sview(at(ident.elements, i));
        out << '\'';
    }
    void visitIdentifierExpr(Ptr<IdentifierExpr> e) {
        printIdentifier(at(e).identifier);
    }
    void visitBinaryOperatorExpr(Ptr<BinaryOperatorExpr> e) {
        out << '\'' << toShortString(at(e).op) << '\'';
    }
    void visitAssignStmt(Ptr<AssignStmt> e) {
        out << '\'' << toShortString(at(e).op) << '\'';
    }
    void visitIntLiteralExpr(Ptr<IntLiteralExpr> e) {
        out << '\'' << at(e).value << '\'';
    }
    void visitVarDecl(Ptr<VarDecl> e) {
        if (at(e).qual == VarDecl::Qualifier::Const)
            out << "const ";
        else if (at(e).qual == VarDecl::Qualifier::Mut)
            out << "mut ";
        out << '\'' << sview(at(e).name) << '\'';
        if (at(e).type) {
            out << ": ";
            printIdentifier(at(e).type);
        }
    }
};
}

void dump(STContext context, Ptr<Stmt> e) {
    STDumper dumper { context, {}, {}, std::cout };
    dumper.dump(e, {});
}
void dump(STContext context, Ptr<Decl> e) {
    STDumper dumper { context, {}, {}, std::cout };
    dumper.dump(e, {});
}