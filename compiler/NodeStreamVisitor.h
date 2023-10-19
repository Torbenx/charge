#pragma once

#include "expr.h"

template<typename Impl, typename DeclResult, typename StmtResult, typename ExprResult>
struct NodeStreamVisitor {
    Node* nodeStream = nullptr;

    Impl* impl() { return static_cast<Impl*>(this); }

private:
    template<typename T>
    using ReturnType = std::conditional_t<std::derived_from<T, Decl>, DeclResult, std::conditional_t<std::derived_from<T, Stmt>, StmtResult, ExprResult>>;
    template<typename T>
    auto invoke(ReturnType<T> (Impl::*f)(std::type_identity_t<T>&), T& node, std::optional<ExprResult>& prevExpr) {
        if constexpr (std::derived_from<T, Stmt>)
            static_assert(!T::IMPLICIT_EXPRESSION_ARGUMENT);
        VERIFY(!prevExpr.has_value());
        return (impl()->*f)(node);
    }
    template<typename T>
    auto invoke(ReturnType<T> (Impl::*f)(std::type_identity_t<T>&, ExprResult), T& node, std::optional<ExprResult>& prevExpr) {
        if constexpr (std::derived_from<T, Stmt>)
            static_assert(T::IMPLICIT_EXPRESSION_ARGUMENT);
        VERIFY(prevExpr.has_value());
        return (impl()->*f)(node, std::move(prevExpr.value()));
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

    template<typename T>
    static constexpr size_t index() {
        if (std::derived_from<T, Decl>)
            return 1;
        if (std::derived_from<T, Stmt>)
            return 2;
        if (std::derived_from<T, Expr>)
            return 3;
        VERIFY_NOT_REACHED();
    }

public:
    using GenericReturnType = std::variant<std::nullopt_t, DeclResult, StmtResult, ExprResult>;
    GenericReturnType visitGeneric(ExpressionPrecedence ambientPrec) {
        VERIFY(nodeStream != nullptr);
        // std::cout << "visit at " << (void*)nodeStream << " - " << nameString(nodeStream->kind()) << '\n';

        GenericReturnType ret = std::nullopt;

        switch (nodeStream->kind()) {

#define NODE(kind, type, prec)                                                       \
    case NodeKind::kind: {                                                           \
        if constexpr (ExpressionPrecedence::prec == ExpressionPrecedence::Primary) { \
            type* thisNode = (type*)nodeStream;                                      \
            nodeStream = thisNode + 1;                                               \
            if constexpr (NodeKind::kind == NodeKind::EmptyNode)                     \
                ret = std::nullopt;                                                  \
            else                                                                     \
                ret.template emplace<index<type>()>(impl()->visit##type(*thisNode)); \
            /* Non-expression primary nodes are returned immediately. */             \
            if constexpr (!std::derived_from<type, Expr>)                            \
                return ret;                                                          \
        } else                                                                       \
            VERIFY_NOT_REACHED();                                                    \
        break;                                                                       \
    }
#include "nodes.h"

        default:
            VERIFY_NOT_REACHED();
        }

        for (;;) {
            // std::cout << "visit at " << (void*)nodeStream << " - " << nameString(nodeStream->kind()) << '\n';

            switch (nodeStream->kind()) {

#define NODE(kind, type, prec)                                                                                \
    case NodeKind::kind: {                                                                                    \
        if constexpr (ExpressionPrecedence::prec == ExpressionPrecedence::Primary) {                          \
            return ret;                                                                                       \
        } else {                                                                                              \
            if (ExpressionPrecedence::prec >= ambientPrec && ambientPrec != ExpressionPrecedence::Statement)  \
                return ret;                                                                                   \
            type* thisNode = (type*)nodeStream;                                                               \
            nodeStream = thisNode + 1;                                                                        \
            ret.template emplace<index<type>()>(impl()->visit##type(*thisNode, std::move(std::get<3>(ret)))); \
        }                                                                                                     \
        break;                                                                                                \
    }
#include "nodes.h"

            default:
                VERIFY_NOT_REACHED();
            }
        }
    }

    DeclResult visitDecl() {
        GenericReturnType result = visitGeneric(ExpressionPrecedence::Statement);
        if (std::holds_alternative<std::nullopt_t>(result))
            return {};
        return std::move(std::get<1>(result));
    }
    std::optional<StmtResult> visitStmt() {
        GenericReturnType result = visitGeneric(ExpressionPrecedence::Statement);
        if (std::holds_alternative<std::nullopt_t>(result))
            return {};
        return std::move(std::get<2>(result));
    }
    std::optional<ExprResult> visitExpr(ExpressionPrecedence ambientPrec) {
        GenericReturnType result = visitGeneric(ambientPrec);
        if (std::holds_alternative<std::nullopt_t>(result))
            return {};
        return std::move(std::get<3>(result));
    }
    auto visitDecl(Node* at) {
        auto s = replaceNodeStream(at);
        return visitDecl();
    }
    auto visitStmt(Node* at) {
        auto s = replaceNodeStream(at);
        return visitStmt();
    }
    auto visitExpr(Node* at) {
        auto s = replaceNodeStream(at);
        return visitExpr(ExpressionPrecedence::Statement);
    }
    auto visitGeneric(Node* at) {
        auto s = replaceNodeStream(at);
        return visitGeneric(ExpressionPrecedence::Statement);
    }
};