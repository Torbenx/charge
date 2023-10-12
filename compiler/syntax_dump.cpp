#include "expr.h"
#include "log.h"
#include <list>
#include <variant>
#include <vector>

template<typename Impl, typename StmtResult, typename ExprResult>
struct NodeStreamVisitor {
    std::vector<ExprResult> exprStack;
    const Node* nodeStream = nullptr;
    NodeStreamVisitor(const Node* stream)
        : nodeStream(stream) { }

    Impl* impl() { return static_cast<Impl*>(this); }

private:
    ExprResult popStack(int_t shrinkSize = 1) {
        ExprResult r = std::move(exprStack.back());
        exprStack.resize(exprStack.size() - shrinkSize);
        return r;
    }

    template<bool isExpr>
    using ReturnType = std::conditional_t<isExpr, ExprResult, StmtResult>;
    template<bool isExpr>
    auto pushOrForward(ReturnType<isExpr>&& in) {
        if constexpr (isExpr)
            exprStack.emplace_back(std::move(in));
        else
            return std::move(in);
    }
    template<typename T, bool isExpr>
    auto invoke(ReturnType<isExpr> (Impl::*f)(const std::type_identity_t<T>&), const T& node) {
        static_assert(T::SUB_EXPRESSION_COUNT == 0);
        return pushOrForward<isExpr>((impl()->*f)(node));
    }
    template<typename T, bool isExpr>
    auto invoke(ReturnType<isExpr> (Impl::*f)(const std::type_identity_t<T>&, ExprResult), const T& node) {
        static_assert(T::SUB_EXPRESSION_COUNT == 1);
        VERIFY(exprStack.size() >= 1);
        return pushOrForward<isExpr>((impl()->*f)(node, popStack()));
    }
    template<typename T, bool isExpr>
    auto invoke(ReturnType<isExpr> (Impl::*f)(const std::type_identity_t<T>&, ExprResult, ExprResult), const T& node) {
        static_assert(T::SUB_EXPRESSION_COUNT == 2);
        VERIFY(exprStack.size() >= 2);
        auto temp = std::move(*(exprStack.end() - 2));
        return pushOrForward<isExpr>((impl()->*f)(node, std::move(temp), popStack(2)));
    }

    using ScopeReturnType = std::pair<std::optional<StmtResult>, std::vector<ExprResult>>;
    ScopeReturnType visitScopeInternal() {
        ScopeReturnType ret {};
        std::swap(ret.second, exprStack);
        bool looping = true;
        while (looping) {
            // std::cout << "visit at " << (void*)*nodeStream << " - " << nameString((*nodeStream)->kind()) << '\n';
            switch (nodeStream->kind()) {

#define NODE(kind, type)
#define EXPR(kind, type)                                   \
    case NodeKind::kind: {                                 \
        const type* thisNode = (const type*)nodeStream;    \
        nodeStream = thisNode + 1;                         \
        invoke<type, true>(&Impl::visit##type, *thisNode); \
        break;                                             \
    }
#define STMT(kind, type)                                                \
    case NodeKind::kind: {                                              \
        const type* thisNode = (const type*)nodeStream;                 \
        nodeStream = thisNode + 1;                                      \
        ret.first = invoke<type, false>(&Impl::visit##type, *thisNode); \
        looping = false;                                                \
        break;                                                          \
    }
#include "nodes.h"

            case NodeKind::EndScope:
                nodeStream = (const EndScope*)nodeStream + 1;
                looping = false;
                break;
            case NodeKind::COUNT:
                VERIFY_NOT_REACHED();
            }
        }
        std::swap(ret.second, exprStack);
        return ret;
    }

public:
    std::vector<ExprResult> visitExprScope() {
        ScopeReturnType result = visitScopeInternal();
        VERIFY(!result.first.has_value());
        return std::move(result.second);
    }
    std::optional<StmtResult> visitSingleStmt() {
        ScopeReturnType result = visitScopeInternal();
        VERIFY(result.second.empty());
        return std::move(result.first);
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
struct Dumper : NodeStreamVisitor<Dumper, DumpResult, DumpResult> {
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

    // statements
    DumpResult visitExpressionStmt(const ExpressionStmt&, DumpResult expr) {
        return expr;
    }
    DumpResult visitUpdateStmt(const UpdateStmt& e, DumpResult base, DumpResult right) {
        right->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitLogicalUpdateStmt(const LogicalUpdateStmt& e, DumpResult base) {
        auto rightStack = visitExprScope();
        VERIFY(rightStack.size() == 1);
        DumpResult right = rightStack[0];
        right->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitLetStmt(const LetStmt&) { VERIFY_NOT_REACHED(); }
    DumpResult visitCompoundStmt(const CompoundStmt& e) {
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
    DumpResult visitIfStmt(const IfStmt& e, DumpResult condition) {
        auto body = visitSingleStmt();
        VERIFY(body.has_value());
        body.value()->lastChild = true;
        return insertBefore(condition, DumpEntry { e.kind() });
    }

    // expressions
    DumpResult visitUnaryOperatorExpr(const UnaryOperatorExpr& e, DumpResult subExpr) {
        subExpr->lastChild = true;
        return insertBefore(subExpr, DumpEntry { e.kind() });
    }
    DumpResult visitBinaryOperatorExpr(const BinaryOperatorExpr& e, DumpResult left, DumpResult right) {
        right->lastChild = true;
        return insertBefore(left, DumpEntry { e.kind() });
    }
    DumpResult visitBinaryLogicalOperatorExpr(const BinaryLogicalOperatorExpr& e, DumpResult left) {
        auto rightStack = visitExprScope();
        VERIFY(rightStack.size() == 1);
        DumpResult right = rightStack[0];
        right->lastChild = true;
        return insertBefore(left, DumpEntry { e.kind() });
    }
    DumpResult visitCallExpr(const CallExpr& e, DumpResult base) {
        std::vector<DumpResult> args = visitExprScope();
        if (args.empty())
            base->lastChild = true;
        else
            args.back()->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitParenthesizedExpr(const ParenthesizedExpr& e) {
        DumpResult out = insertAtEnd(DumpEntry { e.kind() });
        auto args = visitExprScope();
        if (args.size() > 0)
            args.back()->lastChild = true;
        else
            out->leafNode = true;
        return out;
    }
    DumpResult visitAccessExpr(const AccessExpr& e, DumpResult base) {
        base->lastChild = true;
        DumpEntry entry { e.kind() };
        entry.content = fmt::format("'{}{}'", e.kind() == NodeKind::MemberAccessExpr ? "." : "::", wordTable.view(e.accessTarget()).value());
        return insertBefore(base, std::move(entry));
    }
    DumpResult visitIdentifierExpr(const IdentifierExpr& e) {
        DumpEntry entry { e.kind(), true };
        entry.content = fmt::format("'{}'", wordTable.view(e.identifierWord()).value());
        return insertAtEnd(std::move(entry));
    }
    DumpResult visitCompoundExpr(const CompoundExpr& e) {
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
    DumpResult visitIfExpr(const IfExpr& e, DumpResult cond) {
        auto ifTrue = visitExprScope();
        VERIFY(ifTrue.size() == 1);
        ifTrue.front()->lastChild = true;
        return insertBefore(cond, DumpEntry { e.kind() });
    }
    DumpResult visitCommaElseExpr(const CommaElseExpr& e, DumpResult base) {
        auto ifFalse = visitExprScope();
        VERIFY(ifFalse.size() == 1);
        ifFalse.front()->lastChild = true;
        return insertBefore(base, DumpEntry { e.kind() });
    }
    DumpResult visitNumericLiteralExpr(const NumericLiteralExpr&) { VERIFY_NOT_REACHED(); }
    DumpResult visitCharacterLiteralExpr(const CharacterLiteralExpr&) { VERIFY_NOT_REACHED(); }
    DumpResult visitDesignateArgument(const DesignateArgument& e, DumpResult argument) {
        argument->name = e.designatorWord();
        return argument;
    }
    DumpResult visitParameterize(const Parameterize&, DumpResult base) {
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
            out.beginEntry(entry.lastChild, entry.name.empty() ? std::string_view() : wordTable.view(entry.name).value());
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

void dumpStmt(const Node* stream, const WordStringTable& table) {
    Dumper dumper { stream, table };
    auto dumped = dumper.visitSingleStmt();
    VERIFY(dumped.has_value());
    dumped.value()->lastChild = true;
    dumper.print();
}