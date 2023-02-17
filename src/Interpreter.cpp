#include "Parser.h"
#include <algorithm>
#include <optional>
#include <vector>

struct Interpreter : Parser {
    using Parser::Parser;

    static constexpr auto INDICES = []() {
        std::array<uint32_t, 1000> out;
        for (uint32_t i = 0; i < out.size(); i++)
            out[i] = i;
        return out;
    }();
    struct Type {
    };
    struct Value {
        int64_t intValue = 0;
    };

    std::string_view sview(Word w) {
        return context().sview(w);
    }
    bool cmpWord(Word l, Word r) {
        return sview(l) == sview(r);
    }
    bool cmpValue(const Value& l, const Value& r) {
        return l.intValue == r.intValue;
    }

    struct PositionalValue : Value {
        uint32_t index = 0;
    };
    struct ParameterizedDecl {
        Ptr<Decl> decl = {};
        std::vector<PositionalValue> args = {};
        std::vector<uint32_t> params = {};

        bool complete() const { return params.size() == 0; }
    };
    struct LookupContext {
        struct DeclValue {
            ParameterizedDecl decl;
            Value value;
        };
        LookupContext* parent = nullptr;
        Span<Ptr<Decl>> decls;
        std::vector<DeclValue> completeDeclVals;
    };

    struct LookupResult {
        LookupContext* context = nullptr;
        std::vector<ParameterizedDecl> decls;

        bool valid() const { return context != nullptr; }
    };

    std::optional<ParameterizedDecl> substitute(
        LookupContext& declCtx,
        Ptr<Decl> inDecl,
        std::span<const PositionalValue> inArgs,
        std::span<const uint32_t> inParams,
        LookupContext& argsCtx,
        Arguments args) {

        auto a = at(args.args);
        ParameterizedDecl out { inDecl };
        uint32_t ai = 0;
        auto p = at(at(inDecl).parametric.params);
        for (uint32_t pi : inParams) {
            if (!a[ai].target || cmpWord(at(p[pi]).name, a[ai].target)) {
                Value sourceVal = evaluateExpr(argsCtx, a[ai].source);
                Ptr<Identifier> targetTypeId = at(p[pi]).type;
                if (targetTypeId)
                    sourceVal = convert(argsCtx, lookupType(declCtx, at(targetTypeId)), sourceVal);
                out.args.push_back({ sourceVal, pi });
                ai += 1;
            } else {
                out.params.push_back(pi);
            }
        }
        if (ai != a.size())
            return {};
        out.args.insert(out.args.end(), inArgs.begin(), inArgs.end());
        std::sort(out.args.begin(), out.args.end(), [](const PositionalValue& l, const PositionalValue& r) { return l.index < r.index; });
        return std::move(out);
    }
    LookupResult findInCtx(LookupContext& context, LookupContext& identCtx, Identifier& ident) {
        LookupResult out;
        for (Ptr<Decl> decl : at(context.decls)) {
            auto& d = at(decl);
            fmt::println("comparing {} and {}", sview(d.name), sview(ident.name));
            if (cmpWord(d.name, ident.name)) {
                out.context = &context;
                auto sub = substitute(context, decl, {}, std::span(INDICES).subspan(0, d.parametric.params.count), identCtx, ident.args);
                if (sub.has_value())
                    out.decls.push_back(sub.value());
            }
        }
        return out;
    }
    LookupResult lookupIdentifier(LookupContext& identCtx, Identifier& ident) {
        if (ident.base) {
            auto base = lookupIdentifier(identCtx, at(ident.base));
            VERIFY(base.decls.size() == 1);
            auto& decl = base.decls[0];
            VERIFY(decl.complete());
            Value baseValue = getValue(*base.context, decl);
            VERIFY_NOT_REACHED(); // TODO: check baseValue is Type and continue lookup
        } else {
            LookupContext* context = &identCtx;
            while (context) {
                LookupResult r = findInCtx(*context, identCtx, ident);
                if (r.valid()) {
                    VERIFY(r.decls.size() > 0);
                    return r;
                }
                context = context->parent;
            }
            VERIFY_NOT_REACHED();
        }
    }
    Type lookupType(LookupContext& ctx, Identifier& ident) {
        LookupResult r = lookupIdentifier(ctx, ident);
        VERIFY_NOT_REACHED();
    }

    bool cmpCompleteDecls(ParameterizedDecl& l, ParameterizedDecl& r) {
        if (l.decl != r.decl)
            return false;
        EXPECT_EQ(l.args.size(), r.args.size());
        for (uint32_t i = 0; i < l.args.size(); i++) {
            EXPECT_EQ(l.args[i].index, i);
            EXPECT_EQ(r.args[i].index, i);
            if (!cmpValue(l.args[i], r.args[i]))
                return false;
        }
        return true;
    }
    Value getValue(LookupContext& context, ParameterizedDecl& decl) {
        VERIFY(decl.complete());
        for (auto& v : context.completeDeclVals) {
            if (!cmpCompleteDecls(v.decl, decl))
                continue;
            return v.value;
        }
        Value v = initialize(context, decl);
        context.completeDeclVals.push_back({ decl, v });
        return v;
    }
    Value initialize(LookupContext& parent, ParameterizedDecl& decl) {
        Decl& d = at(decl.decl);
        LookupContext context { .parent = &parent, .decls = (Span<Ptr<Decl>>)d.parametric.params };
        auto parameters = at(d.parametric.params);
        EXPECT_EQ(parameters.size(), decl.args.size());
        for (uint32_t i = 0; i < parameters.size(); i++) {
            context.completeDeclVals.push_back({ { parameters[i] }, decl.args[i] });
        }

#define DECL_KIND(kind)  \
    case DeclKind::kind: \
        return init##kind(context, (kind&)d);
        switch (d.kind) {
            ENUMERATE_DECL_KINDS
        default:
            VERIFY_NOT_REACHED();
        }
#undef DECL_KIND
    }
    Value initVarDecl(LookupContext& ctx, VarDecl& d) {
        Value source = evaluateExpr(ctx, d.initializer);
        if (d.type)
            return convert(ctx, lookupType(ctx, at(d.type)), source);
        return source;
    }
    Value initStructDecl(LookupContext&, StructDecl&) {
        VERIFY_NOT_REACHED();
    }
    Value initFnDecl(LookupContext&, FnDecl&) {
        VERIFY_NOT_REACHED();
    }

    Value convert(LookupContext&, Type, Value val) {
        return val;
    }

    Value evaluateExpr(LookupContext& ctx, Ptr<Expr> p) {
        Value v = evaluate(ctx, p);
        return v;
    }
    Value evaluate(LookupContext& ctx, Ptr<Stmt> p) {
        auto& e = at(p);

#define STMT_KIND(kind)  \
    case StmtKind::kind: \
        return eval##kind(ctx, (kind&)e);

        switch (e.kind) {
            ENUMERATE_STMT_KINDS
        default:
            VERIFY_NOT_REACHED();
        }
#undef STMT_KIND
    }

    Value evalIdentifierExpr(LookupContext& ctx, IdentifierExpr& e) {
        auto& ident = at(e.identifier);
        LookupResult r = lookupIdentifier(ctx, ident);

        // hack
        VERIFY(r.decls.size() == 1);
        ParameterizedDecl& decl = r.decls[0];
        VERIFY(decl.complete());
        VERIFY(at(decl.decl).kind != DeclKind::FnDecl);
        return getValue(*r.context, decl);
    }

    Value evalIntLiteralExpr(LookupContext&, IntLiteralExpr& e) {
        return { (int64_t)e.value };
    }

#define DECLARE_EVAL_STUB(kind) \
    Value eval##kind(LookupContext&, kind&) { VERIFY_NOT_REACHED(); }
    DECLARE_EVAL_STUB(UnaryOperatorExpr)
    DECLARE_EVAL_STUB(ParenExpr)
    DECLARE_EVAL_STUB(AccessExpr)
    DECLARE_EVAL_STUB(ImmediateBraceExpr)
    DECLARE_EVAL_STUB(CallExpr)
    DECLARE_EVAL_STUB(BinaryOperatorExpr)
    DECLARE_EVAL_STUB(AssignStmt)
    DECLARE_EVAL_STUB(NullStmt)
    DECLARE_EVAL_STUB(CompoundStmt)
    DECLARE_EVAL_STUB(LetStmt)
};

void testInterpreter() {
    Interpreter it { R"str(
    {
        const x{y} = y;
    }
    x{3}
    )str" };

    EXPECT_EQ(it.tok.kind(), TokenKind::LeftBrace);
    it.advance();

    auto decls = it.beginSpan<Ptr<Decl>>();
    while (it.tok.kind() != TokenKind::RightBrace) {
        auto& d = it.append(decls, {});
        it.parseDecl(d);
    }
    it.advance();
    Interpreter::LookupContext context;
    context.decls = it.finalizeSpan(decls);

    Ptr<Expr> e;
    it.parseBinaryExpr(e);

    for (Ptr<Decl> d : it.at(context.decls))
        dump(it.context(), d);
    dump(it.context(), e);

    Interpreter::Value v = it.evaluateExpr(context, e);
    fmt::println("eval: {}", v.intValue);
}