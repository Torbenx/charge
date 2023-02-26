#include "Parser.h"
#include <algorithm>
#include <optional>
#include <vector>

struct Interpreter : Parser {
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
    struct LookupContext;
    struct CompleteDecl;
    struct StaticLookupContext;
    struct ParameterizedDecl {
        Ptr<Decl> decl = {};
        uint32_t dependentNestLevel = 0;
        StaticLookupContext* staticContext = nullptr;
        std::vector<PositionalValue> args = {};

        ParameterizedDecl() = default;
        ParameterizedDecl(Ptr<Decl> decl, StaticLookupContext* staticContext, std::vector<PositionalValue> args = {}, uint32_t dependentNestLevel = 0)
            : decl(decl), dependentNestLevel(dependentNestLevel), staticContext(staticContext), args(std::move(args)) { }

        bool valid() const { return (bool)decl; }
        bool dependentIn(uint32_t nestLevel) const {
            VERIFY(dependentNestLevel <= nestLevel);
            return dependentNestLevel != 0 && dependentNestLevel == nestLevel;
        }
        bool dependentAtAll() const { return dependentNestLevel > 0; }
    };
    struct CompleteDecl : ParameterizedDecl {
        std::vector<PositionalValue> withArgs;

        CompleteDecl() = default;
        CompleteDecl(
            Ptr<Decl> decl, StaticLookupContext* staticContext = nullptr, std::vector<PositionalValue> args = {},
            std::vector<PositionalValue> withArgs = {}, uint32_t dependentNestLevel = 0)
            : ParameterizedDecl(decl, staticContext, std::move(args), dependentNestLevel), withArgs(std::move(withArgs)) { }
        CompleteDecl(ParameterizedDecl decl, std::vector<PositionalValue> withArgs = {})
            : ParameterizedDecl(std::move(decl)), withArgs(std::move(withArgs)) { }
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
        ParameterizedDecl,
        Builtin,
        Struct,
        Dependent,
    };
    struct parameterized_t { };
    struct complete_t { };
    struct Value {
        CompleteDecl type;
        union {
            ValueArrayHead* memberValues;
            int64_t builtinValue;
            Ptr<Decl> declType;
            struct {
                Ptr<VarDecl> decl;
                uint32_t nestLevel;
            } dependent;
        } u;
        ValueKind kind = ValueKind::Invalid;

        Value()
            : u { .memberValues = nullptr } { }
        Value(Type type, int64_t value)
            : type(std::move(type)), u { .builtinValue = value }, kind(ValueKind::Builtin) { }
        Value(complete_t, Ptr<Decl> type, CompleteDecl decl)
            : type(std::move(decl)), u { .declType = type }, kind(ValueKind::CompleteDecl) { }
        Value(Type type, Ptr<VarDecl> depDecl, uint32_t level)
            : type(std::move(type)), u { .dependent = { depDecl, level } }, kind(ValueKind::Dependent) { }
        Value(Type type, uint32_t memberCount)
            : type(std::move(type))
            , u { .memberValues = (ValueArrayHead*)::operator new(memberCount * sizeof(Value) + sizeof(ValueArrayHead)) }
            , kind(ValueKind::Struct) {
            u.memberValues->refCnt = 1;
            u.memberValues->size = memberCount;
            for (Value& v : u.memberValues->array())
                std::construct_at(&v);
        }
        Value(parameterized_t, Ptr<Decl> type, ParameterizedDecl decl)
            : type(std::move(decl)), u { .declType = type }, kind(ValueKind::ParameterizedDecl) { }

        Value(const Value& other)
            : type(other.type), u(other.u), kind(other.kind) {
        }
        Value& operator=(const Value& other) {
            deref();
            type = other.type;
            u = other.u;
            kind = other.kind;
            ref();
            return *this;
        }
        Value(Value&& other)
            : type(std::move(other.type)), u(other.u), kind(other.kind) {
            other.kind = ValueKind::Invalid;
        }
        Value& operator=(Value&& other) {
            deref();
            type = std::move(other.type);
            u = other.u;
            kind = other.kind;
            other.kind = ValueKind::Invalid;
            return *this;
        }

        bool valid() const { return kind != ValueKind::Invalid; }
        bool dependentIn(uint32_t nestLevel) const {
            if (kind != ValueKind::Dependent)
                return false;
            VERIFY(u.dependent.nestLevel <= nestLevel);
            return u.dependent.nestLevel != 0 && u.dependent.nestLevel == nestLevel;
        }
        bool dependentAtAll() const { return u.dependent.nestLevel > 0; }

        void ref() {
            if (kind == ValueKind::Struct && u.memberValues)
                u.memberValues->refCnt += 1;
        }
        void deref() {
            if (kind == ValueKind::Struct && u.memberValues) {
                u.memberValues->refCnt -= 1;
                if (u.memberValues->refCnt > 0)
                    return;
                for (Value& v : u.memberValues->array())
                    std::destroy_at(&v);
                ::operator delete(u.memberValues);
            }
        }
        ~Value() {
            deref();
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
        bool isStaticContext = false;
        std::span<const Ptr<Decl>> decls = {};
        std::vector<DeclValue> completeDeclVals = {};
        LookupContext(LookupContext* parent)
            : parent(parent) { }
    };
    struct StaticLookupContext : LookupContext {
        CompleteDecl staticDecl;

        StaticLookupContext(StaticLookupContext* parent, CompleteDecl staticDecl)
            : LookupContext(parent), staticDecl(std::move(staticDecl)) { isStaticContext = true; }
    };
    struct LocalLookupContext : LookupContext {
        std::vector<Ptr<Decl>> declsVector;
        LocalLookupContext(LookupContext* parent)
            : LookupContext(parent) { }
        void declare(Ptr<Decl> decl, Value value) {
            declsVector.push_back(decl);
            decls = declsVector;
            completeDeclVals.push_back({ { decl }, value });
        }
    };
    StaticLookupContext* asStaticContext(LookupContext& context) {
        if (context.isStaticContext)
            return static_cast<StaticLookupContext*>(&context);
        return nullptr;
    }

    struct LookupResult : HomogeneousDeclSet {
        LookupContext* context = nullptr;

        bool valid() const { return context != nullptr; }
    };

    enum class ControlFlowKind {
        None,
        Return,
        Break,
        Continue,
    };
    struct ControlFlow {
        ControlFlowKind kind = ControlFlowKind::None;
        Value value = {}; // return value
    };

    StaticLookupContext* globalContext = nullptr;

    Type typeType;
    Type numType;
    Ptr<Decl> arrayDecl;
    Type overloadSetType;
    Type overloadType;
    Type boolType;

    uint32_t dependentNestLevel = 0;
    struct Deduction {
        Ptr<VarDecl> param;
        Value value;
    };
    struct DependentScope {
        Interpreter* i;
        uint32_t level;
        std::vector<Deduction> deductions;
        DependentScope(Interpreter* i)
            : i(i), level(++(i->dependentNestLevel)) { }
        ~DependentScope() {
            EXPECT_EQ(i->dependentNestLevel, level);
            i->dependentNestLevel = level - 1;
        }
    };

    Interpreter() = default;

    void interpretDecls(SourceBuffer buffer) {
        setSourceBuffer(buffer);
        auto decls = beginSpan<Ptr<Decl>>();
        while (tok.kind() != TokenKind::EOS) {
            auto& d = append(decls, {});
            parseDecl(d);
        }
        Ptr<StaticLookupContext> ctx = make<StaticLookupContext>(globalContext, CompleteDecl {});
        globalContext = &at(ctx);
        globalContext->decls = at(finalizeSpan(decls));
    }
    Value interpretExpr(SourceBuffer buffer) {
        setSourceBuffer(buffer);
        Ptr<Expr> e;
        parseBinaryExpr(e);
        return evaluateExpr(*globalContext, e);
    }

    ParameterizedDecl findDeclHelper(const char* name) {
        setSourceBuffer(name);
        Ptr<Expr> e;
        parseBinaryExpr(e);
        VERIFY(at(e).kind == ExprKind::IdentifierExpr);
        LookupResult r = lookupIdentifier(*globalContext, as<IdentifierExpr>(e).identifier);
        EXPECT_EQ(r.decls.size(), 1u);
        return r.decls[0];
    }
    void findBuiltins() {
        numType = asTypeValue(interpretExpr("num"));
        typeType = asTypeValue(interpretExpr("Type"));
        overloadSetType = asTypeValue(interpretExpr("OverloadSet"));
        overloadType = asTypeValue(interpretExpr("Overload"));

        boolType = asTypeValue(interpretExpr("bool"));
        auto falseDecl = findDeclHelper("false");
        VERIFY(at(falseDecl.decl).kind == DeclKind::VarDecl);
        falseDecl.staticContext->completeDeclVals.push_back({ falseDecl, makeBuiltinValue(boolType, 0) });
        auto trueDecl = findDeclHelper("true");
        VERIFY(at(trueDecl.decl).kind == DeclKind::VarDecl);
        trueDecl.staticContext->completeDeclVals.push_back({ trueDecl, makeBuiltinValue(boolType, 1) });

        arrayDecl = findDeclHelper("Array").decl;
        VERIFY(at(arrayDecl).kind == DeclKind::StructDecl);
    }

    Type typeOf(const Value& value) {
        switch (value.kind) {
        case ValueKind::Struct:
        case ValueKind::Builtin:
        case ValueKind::Dependent:
            return value.type;
        case ValueKind::CompleteDecl:
        case ValueKind::ParameterizedDecl:
            return { value.u.declType };
        default:
            VERIFY_NOT_REACHED();
        }
    }
    Type asTypeValue(const Value& value) {
        switch (value.kind) {
        case ValueKind::CompleteDecl:
            VERIFY(value.u.declType == typeType.decl);
            return value.type;
        case ValueKind::Dependent: {
            VERIFY(value.type.decl == typeType.decl);
            VERIFY(value.type.args.empty());
            VERIFY(value.type.withArgs.empty());
            Type ret = { (Ptr<Decl>)value.u.dependent.decl };
            ret.dependentNestLevel = value.u.dependent.nestLevel;
            return ret;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    Value makeBuiltinValue(Type type, int64_t value) { return { std::move(type), value }; }
    Value makeDependentValue(Ptr<VarDecl> decl, Type type, uint32_t nestLevel) {
        return { std::move(type), decl, nestLevel };
    }
    Value makeTypeValue(Type type) {
        return { complete_t(), typeType.decl, std::move(type) };
    }
    Value makeStructValue(Type type, uint32_t memberCount) {
        return Value { std::move(type), memberCount };
    }
    Value makeParameterizedDeclValue(Ptr<Decl> type, ParameterizedDecl decl) {
        return Value { parameterized_t(), type, std::move(decl) };
    }

    std::string_view sview(Word w) {
        return { (const char*)&sourceBuffers[w.bufferId][w.start], w.length };
    }

    bool cmpWord(Word l, Word r) { return sview(l) == sview(r); }
    bool cmpValue(const Value& l, const Value& r) {
        VERIFY(l.kind == r.kind);
        switch (l.kind) {
        case ValueKind::Builtin:
            VERIFY(cmpCompleteDecls(l.type, r.type));
            return l.u.builtinValue == r.u.builtinValue;
        case ValueKind::Struct: {
            VERIFY(cmpCompleteDecls(l.type, r.type));
            EXPECT_EQ(l.u.memberValues->size, r.u.memberValues->size);
            uint32_t size = l.u.memberValues->size;
            for (uint32_t i = 0; i < size; i++) {
                if (!cmpValue(l.u.memberValues->array()[i], r.u.memberValues->array()[i]))
                    return false;
            }
            return true;
        }
        case ValueKind::ParameterizedDecl:
            VERIFY_NOT_REACHED();
        case ValueKind::CompleteDecl:
            return cmpCompleteDecls(l.type, r.type);
        default:
            VERIFY_NOT_REACHED();
        }
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

    std::optional<std::vector<PositionalValue>> positionArguments(Parameters& parameters, std::span<const PositionalValue> inArgs, std::span<const NamedValue> subArgs) {
        std::vector<PositionalValue> out;
        auto params = at(parameters.params);
        uint32_t inOff = 0;
        uint32_t subOff = 0;
        for (uint32_t i = 0; i < params.size(); i++) {
            if (inOff < inArgs.size() && inArgs[inOff].index == i) {
                out.push_back(inArgs[inOff]);
                inOff += 1;
            } else if (subOff < subArgs.size() && (!subArgs[subOff].name || cmpWord(at(params[i]).name, subArgs[subOff].name))) {
                out.push_back({ subArgs[subOff], (uint16_t)i });
                subOff += 1;
            }
        }
        if (subOff == subArgs.size())
            return out;
        return {};
    }
    bool deduceParameters(const Value& source, const Value& target, std::vector<Deduction>& deductions) {
        if (!deduceParameters(typeOf(source), typeOf(target), deductions))
            return false;

        if (target.dependentIn(this->dependentNestLevel)) {
            deductions.push_back({ target.u.dependent.decl, source });
            return true;
        }

        return cmpValue(source, target);
    }
    bool deduceParameters(const Type& source, const Type& target, std::vector<Deduction>& deductions) {
        if (target.dependentIn(this->dependentNestLevel)) {
            deductions.push_back({ (Ptr<VarDecl>)target.decl, makeTypeValue(source) });
            return true;
        }
        if (target.dependentAtAll()) {
            // allow { Q: Type, a: Q, A: Type, c: constant{A, a} }
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
    Value convertAndDeduce(LookupContext&, const Type& targetType, const Value& sourceValue, std::vector<Deduction>& deductions) {
        if (!deduceParameters(typeOf(sourceValue), targetType, deductions))
            return {};
        // TODO: convert
        return sourceValue;
    }
    std::optional<LookupContext> makeParameterContext(LookupContext& parent, const Parameters& params, std::span<const PositionalValue> args, std::vector<Deduction>& deductions) {
        std::span<Ptr<Decl>> allDecls = at((Span<Ptr<Decl>>)params.params);
        LookupContext context { &parent };
        uint32_t argOff = 0;

        for (uint32_t i = 0; i < allDecls.size(); i++) {
            Value arg;
            Type type = asTypeValue(evaluateExpr(context, as<VarDecl>(allDecls[i]).type));
            if (argOff < args.size() && i == args[argOff].index) {
                arg = convertAndDeduce(context, type, args[argOff], deductions);
                if (!arg.valid()) {
                    fmt::print("unable to match '{}' against ", sview(at(type.decl).name));
                    dumpValue(makeTypeValue(typeOf(args[argOff])));
                    return {};
                }
                argOff += 1;
            } else {
                arg = makeDependentValue((Ptr<VarDecl>)allDecls[i], type, this->dependentNestLevel);
            }
            context.decls = allDecls.subspan(0, i + 1);
            context.completeDeclVals.push_back({ { allDecls[i] }, arg });
        }
        EXPECT_EQ(argOff, args.size());

        return context;
    }
    void applyDeductions(LookupContext& context, std::span<const Deduction> deductions) {
        for (auto& val : context.completeDeclVals) {
            for (auto& deduc : deductions) {
                if (val.decl.decl != (Ptr<Decl>)deduc.param)
                    continue;

                EXPECT_EQ(val.decl.args.size(), 0u);
                EXPECT_EQ(val.decl.withArgs.size(), 0u);
                if (val.value.dependentIn(this->dependentNestLevel)) {
                    // value was deduced
                    val.value = deduc.value;
                } else if (!cmpValue(val.value, deduc.value)) {
                    // value was deduced multiple times but not to the same value
                    val.value = {};
                }
            }
        }
    }
    std::optional<std::vector<PositionalValue>> completeParameterContext(Parameters& parameters, LookupContext& context) {
        // context should have been created by makeParameterContext and
        // the values should thus be in the same order as the parameters
        std::span<const Ptr<VarDecl>> params = at(parameters.params);
        EXPECT_EQ(params.size(), context.completeDeclVals.size());

        std::vector<PositionalValue> out;
        for (uint32_t i = 0; i < params.size(); i++) {
            auto& val = context.completeDeclVals[i];
            VERIFY(val.decl.decl == (Ptr<Decl>)params[i]);
            if (!val.value.valid())
                return {};

            Value arg;
            if (val.value.dependentIn(this->dependentNestLevel)) {
                // no argument was provided and nothing was deduced
                // -> use the default arugment
                auto& param = at(params[i]);
                if (!param.initializer)
                    return {};
                arg = evaluateExpr(context, param.initializer);
                if (param.type) {
                    Value type = evaluateExpr(context, param.type);
                    if (!type.valid())
                        return {};
                    arg = convert(context, asTypeValue(type), arg);
                }
            } else {
                // argument shoud already be converted
                arg = val.value;
            }

            out.push_back({ arg, (uint16_t)i });
        }

        return out;
    }
    std::optional<CompleteDecl> completeDecl(LookupContext& declCtx, const ParameterizedDecl& decl) {
        DependentScope depScope { this };
        // fmt::println("completing '{}' at level {}", sview(at(decl.decl).name), depScope.level);

        auto withCtx = makeParameterContext(declCtx, at(decl.decl).with.params, {}, depScope.deductions);
        if (!withCtx.has_value())
            return {};

        auto paramCtx = makeParameterContext(withCtx.value(), at(decl.decl).parametric, decl.args, depScope.deductions);
        if (!paramCtx.has_value())
            return {};

        applyDeductions(withCtx.value(), depScope.deductions);
        applyDeductions(paramCtx.value(), depScope.deductions);

        auto withArgs = completeParameterContext(at(decl.decl).with.params, withCtx.value());
        if (!withArgs.has_value())
            return {};

        auto paramArgs = completeParameterContext(at(decl.decl).parametric, paramCtx.value());
        if (!paramArgs.has_value())
            return {};

        return CompleteDecl { decl.decl, decl.staticContext, std::move(paramArgs.value()), std::move(withArgs.value()) };
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
            ParameterizedDecl parameterized { decl, asStaticContext(context) };
            auto parametricArgs = positionArguments(d.parametric, {}, args);
            if (!parametricArgs.has_value())
                continue;
            parameterized.args = std::move(parametricArgs.value());

            if (out.declKind != DeclKind::Invalid && out.declKind != at(decl).kind) {
                out.decls.clear();
                break;
            }
            out.declKind = at(decl).kind;
            out.decls.push_back(std::move(parameterized));
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
        fmt::println("looking up '{}' failed", sview(ident.word));
        VERIFY_NOT_REACHED();
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
    LookupContext makeCompleteParameterContext(LookupContext& parent, const Parameters& params, std::span<const PositionalValue> args) {
        auto decls = at((Span<Ptr<Decl>>)params.params);
        LookupContext context { &parent };
        context.decls = decls;
        EXPECT_EQ(decls.size(), args.size());
        for (uint32_t i = 0; i < decls.size(); i++) {
            EXPECT_EQ(args[i].index, i);
            context.completeDeclVals.push_back({ { decls[i] }, args[i] });
        }
        return context;
    }
    Value initialize(LookupContext& parent, const CompleteDecl& decl) {
        // FIXME: Only initialize global (static) VarDecls
        const VarDecl& d = (VarDecl&)at(decl.decl);
        LookupContext withCtx = makeCompleteParameterContext(parent, d.with.params, decl.withArgs);
        LookupContext paramCtx = makeCompleteParameterContext(withCtx, d.parametric, decl.args);

        Value source = evaluateExpr(paramCtx, d.initializer);
        if (d.type) {
            Value v = evaluateExpr(paramCtx, d.type);
            VERIFY(v.kind == ValueKind::CompleteDecl);
            VERIFY(v.u.declType == typeType.decl);
            source = convert(paramCtx, asTypeValue(v), source);
        }
        return source;
    }

    Value convert(LookupContext&, Type, Value val) {
        return val;
    }

    Value evaluateExpr(LookupContext& ctx, Ptr<Expr> p) {
        auto& e = at(p);

#define EXPR_KIND(kind)  \
    case ExprKind::kind: \
        return eval##kind(ctx, (kind&)e);

        switch (e.kind) {
            ENUMERATE_EXPR_KINDS
        default:
            VERIFY_NOT_REACHED();
        }
#undef EXPR_KIND
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
            return makeTypeValue(std::move(opt.value()));
        }
        case DeclKind::FnDecl: {
            if (r.decls.size() == 0)
                return {};
            Value v = makeStructValue(overloadSetType, r.decls.size());
            for (uint32_t i = 0; i < r.decls.size(); i++)
                v.u.memberValues->array()[i] = makeParameterizedDeclValue(overloadType.decl, std::move(r.decls[i]));
            return v;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    Value evalIntLiteralExpr(LookupContext&, IntLiteralExpr& e) {
        return makeBuiltinValue(numType, e.value);
    }

    struct CompleteCall {
        CompleteDecl target;
        std::vector<PositionalValue> args;
    };
    std::optional<CompleteCall> completeCall(const ParameterizedDecl& fnDecl, std::span<const NamedValue> namedFnArgs) {
        VERIFY(fnDecl.staticContext != nullptr);
        auto& fn = as<FnDecl>(fnDecl.decl);

        auto posFnArgs = positionArguments(fn.params, {}, namedFnArgs);
        if (!posFnArgs.has_value())
            return {};

        DependentScope depScope { this };

        auto withCtx = makeParameterContext(*fnDecl.staticContext, fn.with.params, {}, depScope.deductions);
        if (!withCtx.has_value())
            return {};

        auto parametricCtx = makeParameterContext(withCtx.value(), fn.parametric, fnDecl.args, depScope.deductions);
        if (!parametricCtx.has_value())
            return {};

        auto fnCtx = makeParameterContext(parametricCtx.value(), fn.params, posFnArgs.value(), depScope.deductions);
        if (!fnCtx.has_value())
            return {};

        applyDeductions(withCtx.value(), depScope.deductions);
        applyDeductions(parametricCtx.value(), depScope.deductions);
        applyDeductions(fnCtx.value(), depScope.deductions);

        auto withArgs = completeParameterContext(fn.with.params, withCtx.value());
        if (!withArgs.has_value())
            return {};

        auto parametricArgs = completeParameterContext(fn.parametric, parametricCtx.value());
        if (!parametricArgs.has_value())
            return {};

        auto fnArgs = completeParameterContext(fn.params, fnCtx.value());
        if (!fnArgs.has_value())
            return {};

        return CompleteCall { { fnDecl.decl, fnDecl.staticContext, std::move(parametricArgs.value()), std::move(withArgs.value()) }, std::move(fnArgs.value()) };
    }
    Value evaluateFunction(CompleteCall call) {
        auto& targetDecl = as<FnDecl>(call.target.decl);
        LookupContext withCtx = makeCompleteParameterContext(*call.target.staticContext, targetDecl.with.params, call.target.withArgs);
        LookupContext parametricCtx = makeCompleteParameterContext(withCtx, targetDecl.parametric, call.target.args);
        LookupContext fnParmsCtx = makeCompleteParameterContext(parametricCtx, targetDecl.params, call.args);
        auto flow = evalCompoundStmt(fnParmsCtx, at(targetDecl.body));
        if (flow.kind == ControlFlowKind::None)
            return {};
        if (flow.kind == ControlFlowKind::Return)
            return flow.value;
        VERIFY_NOT_REACHED();
    }
    Value evalCallExpr(LookupContext& ctx, CallExpr& e) {
        Value base = evaluateExpr(ctx, e.base);
        auto baseType = typeOf(base);
        VERIFY(e.callKind == CallKind::Paren);
        if (cmpCompleteDecls(baseType, typeType)) {
            // hacked constructor: make builtin value
            VERIFY(e.args.args.count == 0);
            return { asTypeValue(base), (int64_t)0 };
        } else if (cmpCompleteDecls(baseType, overloadSetType)) {
            auto args = evaluateArguments(ctx, e.args);
            VERIFY(base.kind == ValueKind::Struct);
            std::optional<CompleteCall> call;
            for (Value& overload : base.u.memberValues->array()) {
                auto c = completeCall(overload.type, args);
                if (c.has_value()) {
                    VERIFY(!call.has_value());
                    call = std::move(c.value());
                }
            }
            VERIFY(call.has_value());
            return evaluateFunction(std::move(call.value()));
        }
        VERIFY_NOT_REACHED();
    }

    Value evalUnaryOperatorExpr(LookupContext&, UnaryOperatorExpr&) { VERIFY_NOT_REACHED(); }
    Value evalParenExpr(LookupContext&, ParenExpr&) { VERIFY_NOT_REACHED(); }
    Value evalAccessExpr(LookupContext&, AccessExpr&) { VERIFY_NOT_REACHED(); }
    Value evalImmediateBraceExpr(LookupContext&, ImmediateBraceExpr&) { VERIFY_NOT_REACHED(); }
    Value evalBinaryOperatorExpr(LookupContext&, BinaryOperatorExpr&) { VERIFY_NOT_REACHED(); }

    ControlFlow evaluateStmt(LocalLookupContext& ctx, Ptr<Stmt> p) {
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

#define PROPEGATE_FLOW(...)                     \
    {                                           \
        ControlFlow flow = __VA_ARGS__;         \
        if (flow.kind != ControlFlowKind::None) \
            return flow;                        \
    }

    ControlFlow evalCompoundStmt(LookupContext& parent, CompoundStmt& body) {
        LocalLookupContext context { &parent };
        for (Ptr<Stmt> stmt : at(body.body)) {
            PROPEGATE_FLOW(evaluateStmt(context, stmt));
        }
        return {};
    }

    ControlFlow evalLetStmt(LocalLookupContext& context, LetStmt& stmt) {
        auto& decl = at(stmt.decl);
        Value init = evaluateExpr(context, decl.initializer);
        if (decl.type) {
            Type type = asTypeValue(evaluateExpr(context, decl.type));
            init = convert(context, type, init);
        }
        context.declare(stmt.decl, init);
        return {};
    }

    ControlFlow evalExprStmt(LookupContext& context, ExprStmt& stmt) {
        evaluateExpr(context, stmt.expr);
        return {};
    }

    ControlFlow evalReturnStmt(LookupContext& context, ReturnStmt& stmt) {
        if (stmt.expr)
            return { ControlFlowKind::Return, evaluateExpr(context, stmt.expr) };
        return { ControlFlowKind::Return };
    }

    ControlFlow evalIfStmt(LookupContext& context, IfStmt& stmt) {
        Value condition = evaluateExpr(context, stmt.condition);
        // TODO: apply conversions
        VERIFY(cmpCompleteDecls(typeOf(condition), boolType));
        VERIFY(condition.kind == ValueKind::Builtin);
        if (condition.u.builtinValue) {
            LocalLookupContext localCtx { &context };
            PROPEGATE_FLOW(evaluateStmt(localCtx, stmt.ifTrue));
        } else if (stmt.ifFalse) {
            LocalLookupContext localCtx { &context };
            PROPEGATE_FLOW(evaluateStmt(localCtx, stmt.ifFalse));
        }
        return {};
    }

    ControlFlow evalNullStmt(LookupContext&, NullStmt&) { return {}; }

    ControlFlow evalAssignStmt(LookupContext&, AssignStmt&) { VERIFY_NOT_REACHED(); }

    void dumpValue(const Value& value) {
        switch (value.kind) {
        case ValueKind::Invalid:
            fmt::println("Invalid Value");
            break;
        case ValueKind::Builtin:
            fmt::println("[{}] {}", sview(at(value.type.decl).name), value.u.builtinValue);
            break;
        case ValueKind::ParameterizedDecl:
        case ValueKind::CompleteDecl:
            fmt::println("[{}] {}{}", sview(at(value.u.declType).name), value.type.dependentAtAll() ? "dependent " : "", sview(at(value.type.decl).name));
            for (auto& arg : value.type.args) {
                fmt::print("  '{}': ", sview(at(at(at(value.type.decl).parametric.params, arg.index)).name));
                dumpValue(arg);
            }
            break;
        case ValueKind::Dependent:
            fmt::println("[{}] dependend {}", sview(at(value.type.decl).name), sview(at(value.u.dependent.decl).name));
            break;
        case ValueKind::Struct:
            fmt::println("[{}] struct with {} members", sview(at(value.type.decl).name), value.u.memberValues->size);
            for (const Value& member : value.u.memberValues->array()) {
                fmt::print("  ");
                dumpValue(member);
            }
            break;
        default:
            VERIFY_NOT_REACHED();
        }
    }
};

void testInterpreter() {
    Interpreter it;
    it.interpretDecls(R"str(
        struct Type{} {}
        struct num{} {}
        struct Array{T: Type} {}
        struct Overload{} {}
        struct OverloadSet{} {}
        struct bool{} {}
        const true: bool = {};
        const false: bool = {};
    )str");
    it.findBuiltins();
    it.interpretDecls(R"str(
        function foo(x: bool) {
            if x
                return foo(false);
            return x;
        }
    )str");

    Interpreter::Value v = it.interpretExpr(
        "foo(true)");
    fmt::print("eval: ");
    it.dumpValue(v);
}