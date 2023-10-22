#include "NodeStreamVisitor.h"
#include "expr.h"
#include "log.h"
#include <list>

template<typename Impl, typename DeclResult>
struct DeclVisitor {
    Impl* impl() { return static_cast<Impl*>(this); }

    DeclResult visitDecl(Decl* decl) {
        switch (decl->kind()) {

#define DECL(kind, type) \
    case DeclKind::kind: \
        return impl()->visit##type(*(type*)decl);
            ENUMERATE_DECL_KINDS
#undef DECL

        default:
            VERIFY_NOT_REACHED();
        }
    }
};

struct DumpEntry {
    std::variant<NodeKind, DeclKind> kind;
    bool leafNode = false;
    bool lastChild = false;
    bool hasBraces = false;
    Word name = {};
    std::string content = {};
};

using DumpResult = std::list<DumpEntry>::iterator;
struct Dumper : NodeStreamVisitor<Dumper, DumpResult, DumpResult>, DeclVisitor<Dumper, DumpResult> {
    const WordStringTable& wordTable;
    std::list<DumpEntry> entries = {};
    DumpResult currentEnd = entries.end();

    Dumper(const WordStringTable& table)
        : wordTable(table) { }

    auto visitExprWithEnd(ExpressionPrecedence prec, DumpResult end) {
        DumpResult savedEnd = currentEnd;
        currentEnd = end;
        auto r = visitExpr(prec);
        currentEnd = savedEnd;
        return r;
    }

    DumpResult insertBefore(DumpResult before, DumpEntry&& entry) {
        VERIFY(before != currentEnd);
        return entries.emplace(before, std::move(entry));
    }
    DumpResult insertAtEnd(DumpEntry&& entry) {
        return entries.emplace(currentEnd, std::move(entry));
    }

    // declarations
    std::optional<DumpResult> visitStaticDeclInternal(StaticDecl& d) {
        std::optional<DumpResult> lastDecl;
        for (Decl* decl : d.decls().all())
            lastDecl = visitDecl(decl);
        return lastDecl;
    }
    DumpResult visitModuleDecl(ModuleDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });

        auto lastDecl = visitStaticDeclInternal(d);
        if (lastDecl.has_value())
            lastDecl.value()->lastChild = true;
        else
            out->leafNode = true;

        return out;
    }
    DumpResult visitNamespaceDecl(NamespaceDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto lastDecl = visitStaticDeclInternal(d);
        if (lastDecl.has_value())
            lastDecl.value()->lastChild = true;
        else
            out->leafNode = true;

        return out;
    }
    DumpResult visitTypeDecl(TypeDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto lastDecl = visitStaticDeclInternal(d);
        if (lastDecl.has_value())
            lastDecl.value()->lastChild = true;
        else
            out->leafNode = true;

        return out;
    }
    DumpResult visitStaticVariableDecl(StaticVariableDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto lastDecl = visitStaticDeclInternal(d);
        auto type = visitExpr(d.typeExpr());
        auto init = visitExpr(d.initExpr());

        if (init.has_value()) {
            init.value()->lastChild = true;
        } else if (type.has_value()) {
            type.value()->lastChild = true;
        } else if (lastDecl.has_value()) {
            lastDecl.value()->lastChild = true;
        } else {
            out->leafNode = true;
        }
        return out;
    }
    DumpResult visitFunctionDecl(FunctionDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto lastDecl = visitStaticDeclInternal(d);
        auto type = visitExpr(d.returnTypeExpr());
        auto body = visitGeneric(d.body());

        if (body.index() != 0) {
            std::visit(
                [](auto v) {
                    if constexpr (std::is_same_v<decltype(v), std::nullopt_t>) {
                        VERIFY_NOT_REACHED();
                    } else {
                        v->lastChild = true;
                    }
                },
                body);
        } else if (type.has_value()) {
            type.value()->lastChild = true;
        } else if (lastDecl.has_value()) {
            lastDecl.value()->lastChild = true;
        } else {
            out->leafNode = true;
        }
        return out;
    }
    DumpResult visitParameterOrMemberDecl(ParameterOrMemberDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto type = visitExpr(d.typeExpr());
        auto init = visitExpr(d.initExpr());
        if (init.has_value()) {
            init.value()->lastChild = true;
        } else if (type.has_value()) {
            type.value()->lastChild = true;
        } else {
            out->leafNode = true;
        }
        return out;
    }
    DumpResult visitHasMemberDecl(HasMemberDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        if (!d.name.empty())
            out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto type = visitExpr(d.typeExpr());
        VERIFY(type.has_value());
        std::optional<DumpResult> lastDecl;
        for (Decl* decl : d.decls().all())
            lastDecl = visitDecl(decl);

        if (lastDecl.has_value()) {
            lastDecl.value()->lastChild = true;
        } else {
            type.value()->lastChild = true;
        }

        return out;
    }
    DumpResult visitBlockVariableDecl(BlockVariableDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto type = visitExpr(d.typeExpr());
        auto init = visitExpr(d.initExpr());
        if (type.has_value()) {
            type.value()->lastChild = true;
        } else if (init.has_value()) {
            init.value()->lastChild = true;
        } else {
            out->leafNode = true;
        }
        return out;
    }

    // statements
    DumpResult visitExpressionStmt(ExpressionStmt&, DumpResult expr) {
        return expr;
    }
    DumpResult visitUpdateStmt(UpdateStmt& e, DumpResult base) {
        auto right = visitExpr(ExpressionPrecedence::Statement);
        right.value()->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitLetStmt(LetStmt& e) {
        DumpResult out = insertAtEnd(DumpEntry { e.kind() });
        DumpResult decl = visitBlockVariableDecl(*e.decl());
        decl->lastChild = true;
        nodeStream = reinterpret_cast<Node*>(e.decl() + 1);
        return out;
    }
    DumpResult visitCompoundStmt(CompoundStmt& e) {
        DumpResult out = insertAtEnd(DumpEntry { e.kind() });
        std::optional<DumpResult> lastStmt;
        for (;;) {
            auto maybeStmt = visitStmt();
            if (!maybeStmt.has_value())
                break;
            lastStmt = maybeStmt.value();
        }
        if (lastStmt.has_value())
            lastStmt.value()->lastChild = true;
        else
            out->leafNode = true;
        return out;
    }
    DumpResult visitIfStmt(IfStmt& e, DumpResult condition) {
        auto body = visitStmt();
        VERIFY(body.has_value());
        body.value()->lastChild = true;
        return insertBefore(condition, DumpEntry { e.kind() });
    }
    DumpResult visitReturnStmt(ReturnStmt& e, DumpResult expr) {
        expr->lastChild = true;
        return insertBefore(expr, DumpEntry { e.kind() });
    }
    DumpResult visitEmptyReturnStmt(EmptyReturnStmt& e) {
        return insertAtEnd(DumpEntry { e.kind(), true });
    }

    // expressions
    DumpResult visitUnaryOperatorExpr(UnaryOperatorExpr& e, DumpResult subExpr) {
        subExpr->lastChild = true;
        return insertBefore(subExpr, DumpEntry { e.kind() });
    }
    DumpResult visitBinaryOperatorExpr(BinaryOperatorExpr& e, DumpResult left) {
        auto right = visitExpr(precedenceOf(e.kind()));
        right.value()->lastChild = true;
        return insertBefore(left, DumpEntry { e.kind() });
    }
    DumpResult visitCallExpr(CallExpr& e, DumpResult base) {
        DumpResult lastChild = base;
        for (;;) {
            auto maybeExpr = visitExpr(ExpressionPrecedence::Statement);
            if (!maybeExpr.has_value())
                break;
            lastChild = maybeExpr.value();
        }
        lastChild->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitParenthesizedExpr(ParenthesizedExpr& e) {
        DumpResult out = insertAtEnd(DumpEntry { e.kind() });
        std::optional<DumpResult> lastChild;
        for (;;) {
            auto maybeExpr = visitExpr(ExpressionPrecedence::Statement);
            if (!maybeExpr.has_value())
                break;
            lastChild = maybeExpr.value();
        }
        if (lastChild.has_value())
            lastChild.value()->lastChild = true;
        else
            out->leafNode = true;
        return out;
    }
    DumpResult visitAccessExpr(AccessExpr& e, DumpResult base) {
        base->lastChild = true;
        DumpEntry entry { e.kind() };
        entry.content = fmt::format("'{}{}'", e.kind() == NodeKind::MemberAccessExpr ? "." : "::", wordTable.view(e.accessTarget()));
        return insertBefore(base, std::move(entry));
    }
    DumpResult visitIdentifierExpr(IdentifierExpr& e) {
        DumpEntry entry { e.kind(), true };
        entry.content = fmt::format("'{}'", wordTable.view(e.identifierWord()));
        return insertAtEnd(std::move(entry));
    }
    DumpResult visitCompoundExpr(CompoundExpr& e) {
        DumpResult out = insertAtEnd(DumpEntry { e.kind() });
        std::optional<DumpResult> lastStmt;
        for (;;) {
            auto maybeStmt = visitStmt();
            if (!maybeStmt.has_value())
                break;
            lastStmt = maybeStmt.value();
        }
        lastStmt.value()->lastChild = true;
        return out;
    }
    DumpResult visitIfExpr(IfExpr& e, DumpResult cond) {
        auto ifTrue = visitExpr(ExpressionPrecedence::ConditionalIf);
        ifTrue.value()->lastChild = true;
        return insertBefore(cond, DumpEntry { e.kind() });
    }
    DumpResult visitCommaElseExpr(CommaElseExpr& e, DumpResult base) {
        auto ifFalse = visitExpr(ExpressionPrecedence::ConditionalElse);
        ifFalse.value()->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitNumericLiteralExpr(NumericLiteralExpr& e) {
        return insertAtEnd(DumpEntry { e.kind(), true });
    }
    DumpResult visitCharacterLiteralExpr(CharacterLiteralExpr& e) {
        return insertAtEnd(DumpEntry { e.kind(), true });
    }
    DumpResult visitDesignateArgument(DesignateArgument& e, DumpResult argument) {
        argument->name = e.designatorWord();
        return argument;
    }
    DumpResult visitParameterize(Parameterize&, DumpResult base) {
        // VERIFY(base->kind == NodeKind::IdentifierExpr || base->kind == NodeKind::MemberAccessExpr || base->kind == NodeKind::StaticAccessExpr);
        DumpResult next = base;
        ++next;
        std::optional<DumpResult> lastArg;
        for (;;) {
            auto arg = visitExprWithEnd(ExpressionPrecedence::Statement, next);
            if (!arg.has_value())
                break;
            lastArg = arg.value();
        }
        if (lastArg.has_value()) {
            base->hasBraces = true;
            lastArg.value()->lastChild = true;
        }
        return base;
    }

    struct OutputStream {
        struct IndentItem {
            uint8_t indentAt;
            bool pastEnd;
        };
        int_t lineOffset = 0;
        std::string indentBeforeLastLevel;
        std::vector<IndentItem> levels;
        OutputStream() {
            levels.push_back({ 0, false });
            levels.push_back({ 0, false });
        }
        OutputStream& operator<<(std::string_view str) {
            std::cout << str;
            lineOffset += str.length();
            return *this;
        }
        void endEntry() {
            std::cout << '\n';
            lineOffset = 0;
        }
        void beginEntry(bool lastChild, std::string_view name = {}) {
            VERIFY(lineOffset == 0);
            *this << indentBeforeLastLevel;
            if (name.empty())
                *this << (lastChild ? "'-" : "|-");
            else
                *this << name << "=";
            if (lastChild)
                levels.back().pastEnd = true;
        }
        void addLevel() {
            VERIFY(lineOffset > (int_t)indentBeforeLastLevel.length());
            if (!levels.back().pastEnd)
                indentBeforeLastLevel += "|";
            indentBeforeLastLevel.resize(lineOffset, ' ');
            VERIFY(lineOffset <= std::numeric_limits<uint8_t>::max());
            levels.push_back({ (uint8_t)lineOffset, false });
        }
        void endTree() {
            while (levels.back().pastEnd) {
                levels.pop_back();
                indentBeforeLastLevel.resize(levels.back().indentAt);
            }
        }
    };

    void print() {
        auto nameString = [](std::variant<NodeKind, DeclKind> kind) -> std::string_view {
            return std::visit([](auto kind) -> std::string_view { return ::nameString(kind); }, kind);
        };

        OutputStream out;
        for (auto entry : entries) {
            out.beginEntry(entry.lastChild, entry.name.empty() ? std::string_view() : wordTable.view(entry.name));
            if (!entry.leafNode)
                out.addLevel();

            out << nameString(entry.kind) << " ";
            if (entry.content.length() > 0)
                out << entry.content;

            if (entry.hasBraces) {
                out << "{";
                out.addLevel();
                out << " }";
            } else if (entry.leafNode) {
                out.endTree();
            }
            out.endEntry();
        }
    }
};

void dump(Node* stream, const WordStringTable& table) {
    Dumper dumper { table };
    auto r = dumper.visitGeneric(stream);
    std::visit([](auto v) {
        if constexpr (!std::is_same_v<decltype(v), std::nullopt_t>) {
            v->lastChild = true;
        }
    },
        r);
    dumper.print();
}

void dump(Decl* decl, const WordStringTable& table) {
    Dumper dumper { table };
    auto r = dumper.visitDecl(decl);
    r->lastChild = true;
    dumper.print();
}