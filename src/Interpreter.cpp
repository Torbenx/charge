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

    /*
     * We destingluish between parameterized and completed declarations:
     * - Parameterized decls are a source level declarations with a list of arguments
     *   for the decls parametric parameters. The Values that are not yet converted to
     *   the target type of parameter.
     * - Completed decls are a source with all converted parametric and 'with' arguments
     *   specified and converted.
     *
     * A 'Type' is a completed 'StructDecl'.
     *
     * A 'LookupContext' stores the values of variables. More precisely its map from
     * completed 'VarDecl's to 'Value's.
     *
     * If an 'IdentifierExpr' names a 'VarDecl' a parameterized decl is obtained by
     * parameterizing the source level decl with the evaluated arguments. Then the decl
     * is completed by converting the arguments to the parameters type and deducing the
     * arguments of the 'with' clause in the process. Then the 'with' arguments are
     * converted to 'with' parameter type.
     *
     * If an 'IdentifierExpr' names a 'StructDecl' or 'FnDecl' the result is the
     * parameterized set of all matches.
     *
     * When a parametric 'FnDecl' is invoked the call arguments are converted to the
     * fucntion parameter type and the unspecified parametric and 'with' arguments are
     * deduced in the process. Then the parametric arguments are converted to parametric
     * parameter type while deducing futher 'with' arguments. Last the 'with' arguments
     * are converted to their parameter type.
     *
     */
    struct Value;
    struct NamedValue;
    struct PositionalValue;
    struct ParameterizedDecl {
        Ptr<Decl> decl = {};
        bool dependend = false;
        std::vector<PositionalValue> args = {};

        ParameterizedDecl() = default;
        ParameterizedDecl(Ptr<Decl> decl, std::vector<PositionalValue> args = {}, bool dependend = false)
            : decl(decl), dependend(dependend), args(std::move(args)) { }

        bool valid() const { return (bool)decl; }
    };
    struct CompleteDecl : ParameterizedDecl {
        std::vector<PositionalValue> withArgs;

        CompleteDecl() = default;
        CompleteDecl(Ptr<Decl> decl, std::vector<PositionalValue> args = {}, std::vector<PositionalValue> withArgs = {}, bool dependend = false)
            : ParameterizedDecl(decl, args, dependend), withArgs(withArgs) { }
    };
    struct Type : CompleteDecl {
        using CompleteDecl::CompleteDecl;
        Type(CompleteDecl decl)
            : CompleteDecl(std::move(decl)) { }
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
        CompleteDecl,
        Builtin,
        Struct,
        Dependent,
    };
    struct Value {
        CompleteDecl type;
        union {
            ValueArrayHead* memberValues = nullptr;
            int64_t builtinValue;
            Ptr<Decl> declType;
            Ptr<VarDecl> dependentDecl;
        };
        ValueKind kind = ValueKind::Invalid;

        Value() { }
        Value(Type type, int64_t value)
            : type(std::move(type)), builtinValue(value), kind(ValueKind::Builtin) { }
        Value(Ptr<Decl> type, CompleteDecl decl)
            : type(std::move(decl)), declType(type), kind(ValueKind::CompleteDecl) { }
        Value(Type type, Ptr<VarDecl> depDecl)
            : type(std::move(type)), dependentDecl(depDecl), kind(ValueKind::Dependent) { }
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
            return value.type;
        case ValueKind::CompleteDecl:
            return { value.declType };
        default:
            VERIFY_NOT_REACHED();
        }
    }
    Type asTypeValue(const Value& value) {
        switch (value.kind) {
        case ValueKind::CompleteDecl:
            VERIFY(value.declType == typeType.decl);
            return value.type;
        case ValueKind::Dependent: {
            VERIFY(value.type.decl == typeType.decl);
            VERIFY(value.type.args.empty());
            VERIFY(value.type.withArgs.empty());
            Type ret = { (Ptr<Decl>)value.dependentDecl };
            ret.dependend = true;
            return ret;
        }
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
            VERIFY(cmpCompleteDecls(l.type, r.type));
            return l.builtinValue == r.builtinValue;
        case ValueKind::Struct: {
            VERIFY(cmpCompleteDecls(l.type, r.type));
            EXPECT_EQ(l.memberValues->size, r.memberValues->size);
            uint32_t size = l.memberValues->size;
            for (uint32_t i = 0; i < size; i++) {
                if (!cmpValue(l.memberValues->array()[i], r.memberValues->array()[i]))
                    return false;
            }
            return true;
        }
        case ValueKind::CompleteDecl:
            return cmpCompleteDecls(l.type, r.type);
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
    Value makeDependentValue(Ptr<VarDecl> decl, Type type) {
        return { std::move(type), decl };
    }
    Value makeTypeValue(Type type) {
        return { typeType.decl, std::move(type) };
    }
    struct Deduction {
        Ptr<VarDecl> param;
        Value value;
    };
    bool deduceParameters(const Value& source, const Value& target, std::vector<Deduction>& deductions) {
        if (!deduceParameters(typeOf(source), typeOf(target), deductions))
            return false;

        if (target.kind == ValueKind::Dependent) {
            deductions.push_back({ target.dependentDecl, source });
            return true;
        }

        return cmpValue(source, target);
    }
    bool deduceParameters(const Type& source, const Type& target, std::vector<Deduction>& deductions) {
        if (target.dependend) {
            deductions.push_back({ (Ptr<VarDecl>)target.decl, makeTypeValue(source) });
            return true;
        }
        if (source.decl != target.decl)
            return false;
        VERIFY(source.args.size() == target.args.size());
        for (uint32_t i = 0; i < source.args.size(); i++) {
            if (!deduceParameters(source.args[i], target.args[i], deductions))
                return false;
        }
        return true;
    }
    std::optional<CompleteDecl> completeDecl(LookupContext& declCtx, const ParameterizedDecl& decl) {
        auto params = at(at(decl.decl).parametric.params);

        auto with = at(at(decl.decl).with.params.params);
        LookupContext withCtx { &declCtx };
        std::vector<Ptr<Decl>> withDecls;
        for (Ptr<VarDecl> withDecl : with) {
            auto& d = at(withDecl);
            Value type = evaluateExpr(withCtx, d.type);
            withDecls.push_back(withDecl);
            withCtx.decls = withDecls;
            withCtx.completeDeclVals.push_back({ { withDecl }, makeDependentValue(withDecl, asTypeValue(type)) });
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
                fmt::print("argument to '{}': ", sview(param.name));
                dumpValue(arg);
            } else if (param.initializer) {
                fmt::print("default argument for '{}': ", sview(param.name));
                arg = evaluateExpr(paramCtx, param.initializer);
                dumpValue(arg);
            } else {
                fmt::println("no argument");
                return {};
            }

            if (param.type) {
                auto target = evaluateExpr(paramCtx, param.type);
                fmt::print("target: ");
                dumpValue(target);
                if (!deduceParameters(typeOf(arg), asTypeValue(target), deductions)) {
                    fmt::println("matching failed");
                    return {};
                }
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
        // fmt::println("looking up '{}'", sview(ident.word));
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

    bool cmpCompleteDecls(const CompleteDecl& l, const CompleteDecl& r) {
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
            context.completeDeclVals.push_back({ { decls[i] }, args[i] });
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
            VERIFY(v.kind == ValueKind::CompleteDecl);
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

    Value evalCallExpr(LookupContext& ctx, CallExpr& e) {
        // hacked construct: make builtin value
        Value base = evaluateExpr(ctx, e.base);
        auto baseType = typeOf(base);
        if (cmpCompleteDecls(baseType, typeType)) {
            VERIFY(e.args.args.count == 0);
            return { asTypeValue(base), (int64_t)0 };
        } else {
            VERIFY_NOT_REACHED();
        }
    }

#define DECLARE_EVAL_STUB(kind) \
    Value eval##kind(LookupContext&, kind&) { VERIFY_NOT_REACHED(); }
    DECLARE_EVAL_STUB(UnaryOperatorExpr)
    DECLARE_EVAL_STUB(ParenExpr)
    DECLARE_EVAL_STUB(AccessExpr)
    DECLARE_EVAL_STUB(ImmediateBraceExpr)
    DECLARE_EVAL_STUB(BinaryOperatorExpr)
    DECLARE_EVAL_STUB(AssignStmt)
    DECLARE_EVAL_STUB(NullStmt)
    DECLARE_EVAL_STUB(CompoundStmt)
    DECLARE_EVAL_STUB(LetStmt)

    void dumpValue(const Value& value) {
        switch (value.kind) {
        case ValueKind::Invalid:
            fmt::println("Invalid Value");
            break;
        case ValueKind::Builtin:
            fmt::println("[{}] {}", sview(at(value.type.decl).name), value.builtinValue);
            break;
        case ValueKind::CompleteDecl:
            // dump(context(), value.type.decl, sview(at(value.declType).name));
            fmt::println("[{}] {}", sview(at(value.declType).name), sview(at(value.type.decl).name));
            break;
        case ValueKind::Dependent:
            fmt::println("[{}] dependend {}", sview(at(value.type.decl).name), sview(at(value.dependentDecl).name));
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
};

void testInterpreter() {
    Interpreter it { R"str(
    {
        struct constant{T: Type, v: T} { }

        with {Q: Type, a: Q, A: Type, b: constant{A, a}, B: Type}
        x{c: constant{B, b}}: A = a;
    }
    x{constant{constant{num, 3}, constant{num, 3}()}()}
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
    fmt::print("eval: ");
    it.dumpValue(v);
}