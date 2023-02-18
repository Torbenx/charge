#include "Parser.h"
#include <algorithm>
#include <optional>
#include <vector>

struct Interpreter : Parser {
    static constexpr auto INDICES = []() {
        std::array<uint32_t, 1000> out;
        for (uint32_t i = 0; i < out.size(); i++)
            out[i] = i;
        return out;
    }();

    static constexpr char BUILTIN_NAMES[] = "num Type";

    struct PositionalValue;
    struct Value;
    struct ParameterizedDecl {
        Ptr<Decl> decl = {};
        std::vector<PositionalValue> args = {};

        bool valid() const { return (bool)decl; }
    };
    struct CompleteDecl : ParameterizedDecl { };
    struct Type : CompleteDecl { };
    struct ValueArrayHead {
        uint32_t refCnt = 0;
        uint32_t size = 0;

        std::span<Value> array() {
            return { (Value*)(this + 1), size };
        }
    };
    struct Value {
        std::vector<PositionalValue> typeArgs = {};
        union {
            ValueArrayHead* memberValues;
            int64_t intValue;
        };
        Ptr<Decl> typeDecl = {};
        bool isTypeValue = false;

        Value()
            : memberValues(nullptr) { }
        Value(Type type, int64_t value)
            : typeArgs(std::move(type.args)), intValue(value), typeDecl(type.decl) { }
        Value(Type type)
            : typeArgs(std::move(type.args)), typeDecl(type.decl), isTypeValue(true) { }
    };
    struct PositionalValue : Value {
        uint16_t index = 0;
    };
    struct NamedValue : Value {
        Word name = {};
    };

    struct LookupContext {
        struct DeclValue {
            CompleteDecl decl;
            Value value;
        };
        LookupContext* parent = nullptr;
        std::span<Ptr<Decl>> decls = {};
        std::vector<DeclValue> completeDeclVals = {};
    };

    struct LookupResult {
        LookupContext* context = nullptr;
        std::vector<ParameterizedDecl> decls;

        bool valid() const { return context != nullptr; }
    };

    Type numType;
    Type typeType;
    LookupContext builtinContext;

    Interpreter(SourceBuffer buffer)
        : Parser(buffer)
        , numType { make<Decl>(DeclKind::Builtin, Word { 0, 3, 1 }) }
        , typeType { make<Decl>(DeclKind::Builtin, Word { 4, 4, 1 }) } {
        auto decls = beginSpan<Ptr<Decl>>();
        append(decls, numType.decl);
        append(decls, typeType.decl);
        builtinContext.decls = at(finalizeSpan(decls));
        builtinContext.completeDeclVals.push_back({ numType, Value(numType) });
        builtinContext.completeDeclVals.push_back({ typeType, Value(typeType) });
    }

    Type typeOf(Value value) {
        if (value.isTypeValue)
            return typeType;
        return { value.typeDecl, std::move(value.typeArgs) };
    }

    Value makeNum(int64_t value) { return { numType, value }; }

    std::string_view sview(Word w) {
        const char* buffer = w.bufferId == 0 ? (const char*)source.buffer : BUILTIN_NAMES;
        return { buffer + w.start, w.length };
    }

    bool cmpWord(Word l, Word r) { return sview(l) == sview(r); }

    bool cmpValue(const Value& l, const Value& r) {
        EXPECT_EQ(l.isTypeValue, r.isTypeValue);
        if (l.isTypeValue)
            return cmpCompleteDecls(l.typeDecl, l.typeArgs, r.typeDecl, r.typeArgs);

        VERIFY(l.typeDecl == r.typeDecl);
        if (l.typeDecl == numType.decl)
            return l.intValue == r.intValue;

        EXPECT_EQ(l.memberValues->size, r.memberValues->size);
        uint32_t size = l.memberValues->size;
        for (uint32_t i = 0; i < size; i++) {
            if (!cmpValue(l.memberValues->array()[i], r.memberValues->array()[i]))
                return false;
        }
        return true;
    }

    std::optional<ParameterizedDecl> substitute(Ptr<Decl> inDecl, std::span<const PositionalValue> inArgs, std::span<NamedValue> subArgs) {
        ParameterizedDecl out { inDecl };
        auto params = at(at(inDecl).parametric.params);
        uint32_t inOff = 0;
        uint32_t subOff = 0;
        for (uint32_t i = 0; i < params.size(); i++) {
            if (inOff < inArgs.size() && inArgs[inOff].index == i) {
                out.args.push_back(inArgs[inOff]);
                inOff += 1;
            } else if (subOff < subArgs.size() && (!subArgs[subOff].name || cmpWord(at(params[i]).name, subArgs[subOff].name))) {
                out.args.push_back({ subArgs[subOff], (uint16_t)i });
                subOff += 1;
            }
        }
        if (subOff == subArgs.size())
            return out;
        return {};
    }
    std::optional<CompleteDecl> completeDecl(LookupContext& declCtx, const ParameterizedDecl& decl) {
        auto params = at(at(decl.decl).parametric.params);
        CompleteDecl out { decl.decl };
        uint32_t argOff = 0;
        LookupContext paramCtx { &declCtx };
        std::vector<Ptr<Decl>> decls;
        for (uint32_t i = 0; i < params.size(); i++) {
            auto& param = at(params[i]);
            Value arg;
            if (i == decl.args[argOff].index) {
                arg = decl.args[argOff];
                argOff += 1;
            } else if (param.initializer) {
                arg = evaluateExpr(paramCtx, param.initializer);
            } else
                return {};

            if (param.type)
                arg = convert(paramCtx, lookupType(paramCtx, at(param.type)), arg);

            out.args.push_back({ arg, (uint16_t)i });
            decls.push_back(params[i]);
            paramCtx.decls = decls;
            paramCtx.completeDeclVals.push_back({ CompleteDecl { params[i] }, arg });
        }
        return out;
    }
    std::vector<NamedValue> evaluateArguments(LookupContext& ctx, Arguments& a) {
        std::vector<NamedValue> out;
        auto args = at(a.args);
        for (auto& arg : args) {
            out.push_back({ evaluateExpr(ctx, arg.source), arg.target });
        }
        return out;
    }
    LookupResult findInCtx(LookupContext& context, LookupContext& identCtx, Identifier& ident) {
        LookupResult out;
        auto args = evaluateArguments(identCtx, ident.args);
        for (Ptr<Decl> decl : context.decls) {
            auto& d = at(decl);
            if (cmpWord(d.name, ident.name)) {
                out.context = &context;
                auto sub = substitute(decl, {}, args);
                if (sub.has_value())
                    out.decls.push_back(sub.value());
            }
        }
        return out;
    }
    LookupResult lookupIdentifier(LookupContext& identCtx, Identifier& ident) {
        if (ident.base) {
            auto base = lookupIdentifier(identCtx, at(ident.base));
            EXPECT_EQ(base.decls.size(), 1u);
            auto decl = completeDecl(*base.context, base.decls[0]).value();
            Value baseValue = getValue(*base.context, decl);
            VERIFY_NOT_REACHED(); // TODO: check baseValue is a Type and continue lookup
        } else {
            fmt::println("looking up '{}'", sview(ident.name));
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
        EXPECT_EQ(r.decls.size(), 1u);
        auto decl = completeDecl(*r.context, r.decls[0]).value();
        auto val = getValue(*r.context, decl);
        VERIFY(val.isTypeValue);
        return { decl.decl, std::move(decl.args) };
    }

    bool cmpCompleteDecls(Ptr<Decl> lDecl, std::span<const PositionalValue> lArgs, Ptr<Decl> rDecl, std::span<const PositionalValue> rArgs) {
        if (lDecl != rDecl)
            return false;
        EXPECT_EQ(lArgs.size(), rArgs.size());
        for (uint32_t i = 0; i < lArgs.size(); i++) {
            EXPECT_EQ(lArgs[i].index, i);
            EXPECT_EQ(rArgs[i].index, i);
            if (!cmpValue(lArgs[i], rArgs[i]))
                return false;
        }
        return true;
    }
    bool cmpCompleteDecls(CompleteDecl& l, CompleteDecl& r) {
        return cmpCompleteDecls(l.decl, l.args, r.decl, r.args);
    }
    Value getValue(LookupContext& context, CompleteDecl& decl) {
        for (auto& v : context.completeDeclVals) {
            if (!cmpCompleteDecls(v.decl, decl))
                continue;
            return v.value;
        }
        Value v = initialize(context, decl);
        context.completeDeclVals.push_back({ decl, v });
        return v;
    }
    Value initialize(LookupContext& parent, CompleteDecl& decl) {
        Decl& d = at(decl.decl);
        LookupContext context { .parent = &parent, .decls = at((Span<Ptr<Decl>>)d.parametric.params) };
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
        fmt::println("evalutating {}", toString(e.kind));

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
        CompleteDecl decl = completeDecl(*r.context, r.decls[0]).value();
        VERIFY(at(decl.decl).kind != DeclKind::FnDecl);
        return getValue(*r.context, decl);
    }

    Value evalIntLiteralExpr(LookupContext&, IntLiteralExpr& e) {
        return makeNum(e.value);
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
        x{T: Type, y: T}: T = y;
    }
    x{num, 3}
    )str" };

    EXPECT_EQ(it.tok.kind(), TokenKind::LeftBrace);
    it.advance();

    auto decls = it.beginSpan<Ptr<Decl>>();
    while (it.tok.kind() != TokenKind::RightBrace) {
        auto& d = it.append(decls, {});
        it.parseDecl(d);
    }
    it.advance();
    Interpreter::LookupContext context { &it.builtinContext };
    context.decls = it.at(it.finalizeSpan(decls));

    Ptr<Expr> e;
    it.parseBinaryExpr(e);

    for (Ptr<Decl> d : context.decls)
        dump(it.context(), d);
    dump(it.context(), e);

    Interpreter::Value v = it.evaluateExpr(context, e);
    fmt::println("eval: {}", v.intValue);
}