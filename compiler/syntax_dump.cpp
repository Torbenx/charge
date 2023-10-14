#include "expr.h"
#include "log.h"
#include <list>
#include <variant>
#include <vector>

template<typename Impl, typename DeclResult, typename StmtResult, typename ExprResult>
struct NodeStreamVisitor {
    std::vector<ExprResult> exprStack;
    Node* nodeStream = nullptr;
    NodeStreamVisitor(Node* stream)
        : nodeStream(stream) { }

    Impl* impl() { return static_cast<Impl*>(this); }

private:
    ExprResult popStack(int_t shrinkSize = 1) {
        ExprResult r = std::move(exprStack.back());
        exprStack.resize(exprStack.size() - shrinkSize);
        return r;
    }

    template<typename T>
    using ReturnType = std::conditional_t<std::derived_from<T, Decl>, DeclResult, std::conditional_t<std::derived_from<T, Stmt>, StmtResult, ExprResult>>;
    template<typename T>
    auto pushOrForward(ReturnType<T>&& in) {
        // push expressions and forward declarations and statements
        if constexpr (std::derived_from<T, Expr>)
            exprStack.emplace_back(std::move(in));
        else
            return std::move(in);
    }
    template<typename T>
    auto invoke(ReturnType<T> (Impl::*f)(std::type_identity_t<T>&), T& node) {
        static_assert(T::SUB_EXPRESSION_COUNT == 0);
        return pushOrForward<T>((impl()->*f)(node));
    }
    template<typename T>
    auto invoke(ReturnType<T> (Impl::*f)(std::type_identity_t<T>&, ExprResult), T& node) {
        static_assert(T::SUB_EXPRESSION_COUNT == 1);
        VERIFY(exprStack.size() >= 1);
        return pushOrForward<T>((impl()->*f)(node, popStack()));
    }
    template<typename T>
    auto invoke(ReturnType<T> (Impl::*f)(std::type_identity_t<T>&, ExprResult, ExprResult), T& node) {
        static_assert(T::SUB_EXPRESSION_COUNT == 2);
        VERIFY(exprStack.size() >= 2);
        auto temp = std::move(*(exprStack.end() - 2));
        return pushOrForward<T>((impl()->*f)(node, std::move(temp), popStack(2)));
    }

    struct SavedNodeStream {
        NodeStreamVisitor* thiz;
        Node* saved;
        ~SavedNodeStream() {
            thiz->nodeStream = saved;
        }
    };
    SavedNodeStream replaceNodeStream(Node* newStream) {
        // fmt::print("visit nodes at {}", (void*)newStream);
        // fmt::println(" - {}", nameString(newStream->kind()));
        Node* saved = nodeStream;
        nodeStream = newStream;
        return { this, saved };
    }

public:
    struct ScopeReturnType {
        std::optional<DeclResult> declaration;
        std::optional<StmtResult> statement;
        std::vector<ExprResult> expressions;
    };
    ScopeReturnType visitGenericScope() {
        ScopeReturnType ret {};
        std::swap(ret.expressions, exprStack);
        bool looping = true;
        while (looping) {
            // std::cout << "visit at " << (void*)*nodeStream << " - " << nameString((*nodeStream)->kind()) << '\n';
            switch (nodeStream->kind()) {

#define NODE(kind, type)
#define DECL(kind, type)                                               \
    case NodeKind::kind: {                                             \
        type* thisNode = (type*)nodeStream;                            \
        nodeStream = thisNode + 1;                                     \
        ret.declaration = invoke<type>(&Impl::visit##type, *thisNode); \
        looping = false;                                               \
        break;                                                         \
    }
#define STMT(kind, type)                                             \
    case NodeKind::kind: {                                           \
        type* thisNode = (type*)nodeStream;                          \
        nodeStream = thisNode + 1;                                   \
        ret.statement = invoke<type>(&Impl::visit##type, *thisNode); \
        looping = false;                                             \
        break;                                                       \
    }
#define EXPR(kind, type)                             \
    case NodeKind::kind: {                           \
        type* thisNode = (type*)nodeStream;          \
        nodeStream = thisNode + 1;                   \
        invoke<type>(&Impl::visit##type, *thisNode); \
        break;                                       \
    }
#include "nodes.h"

            case NodeKind::EndScope:
                nodeStream = (EndScope*)nodeStream + 1;
                looping = false;
                break;
            case NodeKind::COUNT:
                VERIFY_NOT_REACHED();
            }
        }
        std::swap(ret.expressions, exprStack);
        return ret;
    }

    DeclResult visitSingleDecl() {
        ScopeReturnType result = visitGenericScope();
        VERIFY(result.declaration.has_value());
        VERIFY(!result.statement.has_value());
        VERIFY(result.expressions.empty());
        return std::move(result.declaration.value());
    }
    std::optional<StmtResult> visitSingleStmt() {
        ScopeReturnType result = visitGenericScope();
        VERIFY(!result.declaration.has_value());
        VERIFY(result.expressions.empty());
        return std::move(result.statement);
    }
    std::vector<ExprResult> visitExprScope() {
        ScopeReturnType result = visitGenericScope();
        VERIFY(!result.declaration.has_value());
        VERIFY(!result.statement.has_value());
        return std::move(result.expressions);
    }
    auto visitSingleDecl(Node* at) {
        auto s = replaceNodeStream(at);
        return visitSingleDecl();
    }
    auto visitSingleStmt(Node* at) {
        auto s = replaceNodeStream(at);
        return visitSingleStmt();
    }
    auto visitExprScope(Node* at) {
        auto s = replaceNodeStream(at);
        return visitExprScope();
    }
    auto visitGenericScope(Node* at) {
        auto s = replaceNodeStream(at);
        return visitGenericScope();
    }
};

struct DumpEntry {
    NodeKind kind;
    bool leafNode = false;
    bool lastChild = false;
    bool hasBraces = false;
    Word name = {};
    std::string content = {};
};
using DumpResult = std::list<DumpEntry>::iterator;
struct Dumper : NodeStreamVisitor<Dumper, DumpResult, DumpResult, DumpResult> {
    const WordStringTable& wordTable;
    std::list<DumpEntry> entries = {};
    DumpResult currentEnd = entries.end();

    auto visitScopeWithEnd(DumpResult end) {
        DumpResult savedEnd = currentEnd;
        currentEnd = end;
        auto r = visitExprScope();
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
            lastDecl = visitSingleDecl(decl);
        return lastDecl;
    }
    DumpResult visitStaticDecl(StaticDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto lastDecl = visitStaticDeclInternal(d);
        if (lastDecl.has_value())
            lastDecl.value()->lastChild = true;
        else
            out->leafNode = true;

        return out;
    }
    DumpResult visitVariableOrFunctionDecl(VariableOrFunctionDecl& d) {
        DumpResult out = insertAtEnd(DumpEntry { d.kind() });
        out->content = fmt::format("'{}'", wordTable.view(d.name));

        auto lastDecl = visitStaticDeclInternal(d);
        auto type = visitExprScope(d.returnOrTypeExpr());
        auto body = visitGenericScope(d.bodyOrInitExpr());
        VERIFY(!body.declaration.has_value());

        if (body.statement.has_value()) {
            VERIFY(body.expressions.empty());
            body.statement.value()->lastChild = true;
        } else if (!body.expressions.empty()) {
            body.expressions.front()->lastChild = true;
        } else if (!type.empty()) {
            VERIFY(type.size() == 1);
            type.back()->lastChild = true;
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

        auto type = visitExprScope(d.typeExpr());
        auto init = visitExprScope(d.initExpr());
        if (!init.empty()) {
            VERIFY(init.size() == 1);
            init.back()->lastChild = true;
        } else if (!type.empty()) {
            VERIFY(type.size() == 1);
            type.back()->lastChild = true;
        } else {
            out->leafNode = true;
        }
        return out;
    }

    // statements
    DumpResult visitExpressionStmt(ExpressionStmt&, DumpResult expr) {
        return expr;
    }
    DumpResult visitUpdateStmt(UpdateStmt& e, DumpResult base, DumpResult right) {
        right->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitLogicalUpdateStmt(LogicalUpdateStmt& e, DumpResult base) {
        auto rightStack = visitExprScope();
        VERIFY(rightStack.size() == 1);
        DumpResult right = rightStack[0];
        right->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitLetStmt(LetStmt&) { VERIFY_NOT_REACHED(); }
    DumpResult visitCompoundStmt(CompoundStmt& e) {
        DumpResult out = insertAtEnd(DumpEntry { e.kind() });
        std::optional<DumpResult> lastStmt;
        for (;;) {
            auto maybeStmt = visitSingleStmt();
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
        auto body = visitSingleStmt();
        VERIFY(body.has_value());
        body.value()->lastChild = true;
        return insertBefore(condition, DumpEntry { e.kind() });
    }

    // expressions
    DumpResult visitUnaryOperatorExpr(UnaryOperatorExpr& e, DumpResult subExpr) {
        subExpr->lastChild = true;
        return insertBefore(subExpr, DumpEntry { e.kind() });
    }
    DumpResult visitBinaryOperatorExpr(BinaryOperatorExpr& e, DumpResult left, DumpResult right) {
        right->lastChild = true;
        return insertBefore(left, DumpEntry { e.kind() });
    }
    DumpResult visitBinaryLogicalOperatorExpr(BinaryLogicalOperatorExpr& e, DumpResult left) {
        auto rightStack = visitExprScope();
        VERIFY(rightStack.size() == 1);
        DumpResult right = rightStack[0];
        right->lastChild = true;
        return insertBefore(left, DumpEntry { e.kind() });
    }
    DumpResult visitCallExpr(CallExpr& e, DumpResult base) {
        std::vector<DumpResult> args = visitExprScope();
        if (args.empty())
            base->lastChild = true;
        else
            args.back()->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitParenthesizedExpr(ParenthesizedExpr& e) {
        DumpResult out = insertAtEnd(DumpEntry { e.kind() });
        auto args = visitExprScope();
        if (args.size() > 0)
            args.back()->lastChild = true;
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
            auto maybeStmt = visitSingleStmt();
            if (!maybeStmt.has_value())
                break;
            lastStmt = maybeStmt.value();
        }
        lastStmt.value()->lastChild = true;
        return out;
    }
    DumpResult visitIfExpr(IfExpr& e, DumpResult cond) {
        auto ifTrue = visitExprScope();
        VERIFY(ifTrue.size() == 1);
        ifTrue.front()->lastChild = true;
        return insertBefore(cond, DumpEntry { e.kind() });
    }
    DumpResult visitCommaElseExpr(CommaElseExpr& e, DumpResult base) {
        auto ifFalse = visitExprScope();
        VERIFY(ifFalse.size() == 1);
        ifFalse.front()->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitNumericLiteralExpr(NumericLiteralExpr&) { VERIFY_NOT_REACHED(); }
    DumpResult visitCharacterLiteralExpr(CharacterLiteralExpr&) { VERIFY_NOT_REACHED(); }
    DumpResult visitDesignateArgument(DesignateArgument& e, DumpResult argument) {
        argument->name = e.designatorWord();
        return argument;
    }
    DumpResult visitParameterize(Parameterize&, DumpResult base) {
        VERIFY(base->kind == NodeKind::IdentifierExpr || base->kind == NodeKind::MemberAccessExpr || base->kind == NodeKind::StaticAccessExpr);
        DumpResult next = base;
        ++next;
        auto args = visitScopeWithEnd(next);
        if (args.size() > 0) {
            base->hasBraces = true;
            args.back()->lastChild = true;
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
    Dumper dumper { stream, table };
    auto r = dumper.visitGenericScope();
    if (r.declaration.has_value())
        r.declaration.value()->lastChild = true;
    else if (r.statement.has_value())
        r.statement.value()->lastChild = true;
    else if (r.expressions.size() > 0)
        r.expressions.back()->lastChild = true;
    else
        VERIFY_NOT_REACHED();
    dumper.print();
}