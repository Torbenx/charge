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

    struct Value;
    struct NamedValue;
    struct PositionalValue;
    struct ParameterizedDecl {
        Ptr<Decl> decl = {};
        std::vector<PositionalValue> args = {};

        ParameterizedDecl(Ptr<Decl> decl, std::vector<PositionalValue> args = {})
            : decl(decl), args(std::move(args)) { }

        bool valid() const { return (bool)decl; }
    };
    struct CompleteDecl : ParameterizedDecl {
        using ParameterizedDecl::ParameterizedDecl;
        std::vector<PositionalValue> withArgs;
    };
    struct Type : CompleteDecl {
        using CompleteDecl::CompleteDecl;
    };
    struct ValueArrayHead {
        uint32_t refCnt = 0;
        uint32_t size = 0;

        std::span<Value> array() {
            return { (Value*)(this + 1), size };
        }
    };
    enum class ValueKind : uint8_t {
        Invalid,
        Decl,
        Builtin,
        Struct,
        Dependent,
    };
    struct Value {
        std::vector<PositionalValue> typeArgs = {};
        union {
            ValueArrayHead* memberValues = nullptr;
            int64_t builtinValue;
            Ptr<Decl> declType;
            Ptr<VarDecl> dependentDecl;
        };
        Ptr<Decl> typeDecl = {};
        ValueKind kind = ValueKind::Invalid;

        Value() { }
        Value(Type type, int64_t value)
            : typeArgs(std::move(type.args)), builtinValue(value), typeDecl(type.decl), kind(ValueKind::Builtin) { }
        Value(Ptr<Decl> type, ParameterizedDecl decl)
            : typeArgs(std::move(decl.args)), declType(type), typeDecl(decl.decl), kind(ValueKind::Decl) { }
        Value(Ptr<VarDecl> depDecl, ParameterizedDecl type)
            : typeArgs(std::move(type.args)), dependentDecl(depDecl), kind(ValueKind::Dependent) { }

        ParameterizedDecl asDecl() const {
            return { typeDecl, typeArgs };
        }
    };
    struct PositionalValue : Value {
        uint16_t index = 0;
    };
    struct NamedValue : Value {
        Word name = {};
    };

    struct HomogeneousDeclSet {
        std::vector<ParameterizedDecl> decls;
        DeclKind declKind = DeclKind::Invalid;
    };

    struct LookupContext {
        struct DeclValue {
            CompleteDecl decl;
            Value value;
        };
        LookupContext* parent = nullptr;
        std::span<const Ptr<Decl>> decls = {};
        std::vector<DeclValue> completeDeclVals = {};
    };
    struct LookupResult : HomogeneousDeclSet {
        LookupContext* context = nullptr;

        bool valid() const { return context != nullptr; }
    };

    Type numType;
    Type typeType;
    LookupContext builtinContext;

    Interpreter(SourceBuffer buffer)
        : Parser(buffer)
        , numType { make<StructDecl>(Word { 0, 3, 1 }) }
        , typeType { make<StructDecl>(Word { 4, 4, 1 }) } {
        auto decls = beginSpan<Ptr<Decl>>();
        append(decls, numType.decl);
        append(decls, typeType.decl);
        builtinContext.decls = at(finalizeSpan(decls));
    }

    Type typeOf(const Value& value) {
        switch (value.kind) {
        case ValueKind::Struct:
        case ValueKind::Builtin:
        case ValueKind::Dependent:
            return { value.typeDecl, value.typeArgs };
        case ValueKind::Decl:
            return { value.declType, {} };
        default:
            VERIFY_NOT_REACHED();
        }
    }

    Value makeNum(int64_t value) { return { numType, value }; }

    std::string_view sview(Word w) {
        const char* buffer = w.bufferId == 0 ? (const char*)source.buffer : BUILTIN_NAMES;
        return { buffer + w.start, w.length };
    }

    bool cmpWord(Word l, Word r) { return sview(l) == sview(r); }

    bool cmpValue(const Value& l, const Value& r) {
        VERIFY(l.kind == r.kind);
        switch (l.kind) {
        case ValueKind::Builtin:
            VERIFY(l.typeDecl == r.typeDecl);
            return l.builtinValue == r.builtinValue;
        case ValueKind::Struct: {
            VERIFY(l.typeDecl == r.typeDecl);
            EXPECT_EQ(l.memberValues->size, r.memberValues->size);
            uint32_t size = l.memberValues->size;
            for (uint32_t i = 0; i < size; i++) {
                if (!cmpValue(l.memberValues->array()[i], r.memberValues->array()[i]))
                    return false;
            }
            return true;
        }
        case ValueKind::Decl:
            return cmpCompleteDecls(l.typeDecl, l.typeArgs, r.typeDecl, r.typeArgs);
        default:
            VERIFY_NOT_REACHED();
        }
    }

    std::optional<ParameterizedDecl> bindArguments(Ptr<Decl> inDecl, std::span<const PositionalValue> inArgs, std::span<const NamedValue> subArgs) {
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
    Value makeDependentValue(Ptr<VarDecl> decl, ParameterizedDecl type) {
        return { decl, std::move(type) };
    }
    Value makeTypeValue(Type type) {
        return { typeType.decl, std::move(type) };
    }
    struct Deduction {
        Ptr<VarDecl> param;
        Value value;
    };
    bool deduceParameters(const Value& source, const Value& target, std::vector<Deduction>& deductions) {
        VERIFY_NOT_REACHED();
        // deduceParameters(typeOf(source), );
        if (target.kind == ValueKind::Dependent) {
            deductions.push_back({ target.dependentDecl, source });
            return true;
        } else {
            // compare source and target
            VERIFY_NOT_REACHED();
        }
    }
    bool deduceParameters(const Type& source, const Value& target, std::vector<Deduction>& deductions) {
        switch (target.kind) {
        case ValueKind::Dependent:
            fmt::println("target is dependent");
            deductions.push_back({ target.dependentDecl, makeTypeValue(source) });
            return true;
        case ValueKind::Decl:
            VERIFY(target.declType == typeType.decl);
            fmt::println("target is decl");
            if (source.decl != target.typeDecl)
                return false;
            if (target.typeArgs.size() != source.args.size())
                return false;
            for (uint32_t i = 0; i < source.args.size(); i++) {
                if (!deduceParameters(source.args[i], target.typeArgs[i], deductions))
                    return false;
            }
            return true;
        default:
            VERIFY_NOT_REACHED();
        }
    }
    std::optional<CompleteDecl> completeDecl(LookupContext& declCtx, const ParameterizedDecl& decl) {
        auto params = at(at(decl.decl).parametric.params);

        auto with = at(at(decl.decl).with.params.params);
        LookupContext withCtx { &declCtx };
        std::vector<Ptr<Decl>> withDecls;
        for (Ptr<VarDecl> withDecl : with) {
            withDecls.push_back(withDecl);
            auto& d = at(withDecl);
            Value type = evaluateExpr(withCtx, d.type);
            VERIFY(type.kind == ValueKind::Decl);
            VERIFY(type.declType == typeType.decl);
            withCtx.completeDeclVals.push_back({ { withDecl }, makeDependentValue(withDecl, type.asDecl()) });
            withCtx.decls = withDecls;
        }

        CompleteDecl out { decl.decl };
        std::vector<Ptr<Decl>> decls;
        LookupContext paramCtx { &withCtx };
        std::vector<Deduction> deductions;
        uint32_t argOff = 0;
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

            if (param.type) {
                auto target = evaluateExpr(paramCtx, param.type);
                auto source = typeOf(arg);
                if (!deduceParameters(source, target, deductions))
                    return {};
            }

            out.args.push_back({ arg, (uint16_t)i });
            decls.push_back(params[i]);
            paramCtx.decls = decls;
            paramCtx.completeDeclVals.push_back({ CompleteDecl { params[i] }, arg });
        }

        for (uint32_t i = 0; i < with.size(); i++) {
            for (const Deduction& deduct : deductions) {
                if (deduct.param != with[i])
                    continue;
                if (out.withArgs.size() <= i)
                    out.withArgs.push_back({ deduct.value, (uint16_t)i });
                else if (!cmpValue(out.withArgs[i], deduct.value))
                    return {};
            }
            if (out.withArgs.size() != i + 1)
                return {};
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
    LookupResult lookupIdentifierIn(LookupContext& context, Word name, std::span<const NamedValue> args) {
        LookupResult out;
        for (Ptr<Decl> decl : context.decls) {
            auto& d = at(decl);
            if (!cmpWord(d.name, name))
                continue;

            out.context = &context;
            auto sub = bindArguments(decl, {}, args);
            if (!sub.has_value())
                continue;

            if (out.declKind != DeclKind::Invalid && out.declKind != at(decl).kind) {
                out.decls.clear();
                break;
            }
            out.declKind = at(decl).kind;
            out.decls.push_back(sub.value());
        }
        return out;
    }
    LookupResult lookupIdentifier(LookupContext& identCtx, Identifier ident) {
        fmt::println("looking up '{}'", sview(ident.word));
        auto args = evaluateArguments(identCtx, ident);
        LookupContext* context = &identCtx;
        while (context) {
            LookupResult r = lookupIdentifierIn(*context, ident.word, args);
            if (r.valid()) {
                VERIFY(r.decls.size() > 0);
                return r;
            }
            context = context->parent;
        }
        VERIFY_NOT_REACHED();
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
    bool cmpCompleteDecls(const CompleteDecl& l, const CompleteDecl& r) {
        return cmpCompleteDecls(l.decl, l.args, r.decl, r.args);
    }
    Value getValue(LookupContext& context, const CompleteDecl& decl) {
        VERIFY(at(decl.decl).kind == DeclKind::VarDecl);
        for (auto& v : context.completeDeclVals) {
            if (!cmpCompleteDecls(v.decl, decl))
                continue;
            return v.value;
        }
        Value v = initialize(context, decl);
        context.completeDeclVals.push_back({ decl, v });
        return v;
    }
    LookupContext makeParameterContext(LookupContext& parent, Span<Ptr<VarDecl>> decls, std::span<const PositionalValue> args) {
        return makeParameterContext(parent, at((Span<Ptr<Decl>>)decls), args);
    }
    LookupContext makeParameterContext(LookupContext& parent, std::span<const Ptr<Decl>> decls, std::span<const PositionalValue> args) {
        LookupContext context { .parent = &parent, .decls = decls };
        EXPECT_EQ(decls.size(), args.size());
        for (uint32_t i = 0; i < decls.size(); i++) {
            EXPECT_EQ(args[i].index, i);
            context.completeDeclVals.push_back({{ decls[i] }, args[i] });
        }
        return context;
    }
    Value initialize(LookupContext& parent, const CompleteDecl& decl) {
        const VarDecl& d = (VarDecl&)at(decl.decl);
        LookupContext withCtx = makeParameterContext(parent, d.with.params.params, decl.withArgs);
        LookupContext paramCtx = makeParameterContext(withCtx, d.parametric.params, decl.args);

        Value source = evaluateExpr(paramCtx, d.initializer);
        if (d.type) {
            Value v = evaluateExpr(paramCtx, d.type);
            VERIFY(v.kind == ValueKind::Decl);
            VERIFY(v.declType == typeType.decl);
            // source = convert(ctx, v.asType(), source);
        }
        return source;
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
        LookupResult r = lookupIdentifier(ctx, e.identifier);

        switch (r.declKind) {
        case DeclKind::VarDecl: {
            if (r.decls.size() != 1)
                return {};
            auto opt = completeDecl(*r.context, r.decls[0]);
            if (!opt.has_value())
                return {};
            return getValue(*r.context, opt.value());
        }
        case DeclKind::StructDecl: {
            if (r.decls.size() != 1)
                return {};
            auto opt = completeDecl(*r.context, r.decls[0]);
            if (!opt.has_value())
                return {};
            return { typeType.decl, std::move(opt.value()) };
        }
        default:
            VERIFY_NOT_REACHED();
        }
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

    void dumpValue(const Value& value) {
        switch (value.kind) {
        case ValueKind::Builtin:
            fmt::println("{}: {}", sview(at(value.typeDecl).name), value.builtinValue);
            break;
        case ValueKind::Decl:
            dump(context(), value.typeDecl, sview(at(value.declType).name));
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
};

void testInterpreter() {
    Interpreter it { R"str(
    {
        with {T: Type}
        x{y: T}: T = y;
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
    Interpreter::LookupContext context { &it.builtinContext };
    context.decls = it.at(it.finalizeSpan(decls));

    Ptr<Expr> e;
    it.parseBinaryExpr(e);

    for (Ptr<Decl> d : context.decls)
        dump(it.context(), d);
    dump(it.context(), e);

    Interpreter::Value v = it.evaluateExpr(context, e);
    fmt::println("eval:");
    it.dumpValue(v);
}