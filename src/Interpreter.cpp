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
    struct ValueArray {
        static ValueArray* make(uint32_t size) {
            auto* out = (ValueArray*)::operator new(size * sizeof(Value) + sizeof(ValueArray));
            out->refCnt = 1;
            out->size = size;
            for (Value& v : out->array())
                std::construct_at(&v);
            return out;
        }

        static void ref(ValueArray* array) {
            if (!array)
                return;
            array->refCnt += 1;
        }
        static void deref(ValueArray* array) {
            if (!array)
                return;
            array->refCnt -= 1;
            if (array->refCnt > 0)
                return;
            for (Value& v : array->array())
                std::destroy_at(&v);
            ::operator delete(array);
        }

        uint32_t refCnt = 0;
        uint32_t size = 0;

        std::span<Value> array() {
            return { (Value*)(this + 1), size };
        }
    };
    struct CompleteDeclBase {
        Ptr<Decl> decl = {};
        uint16_t dependentNestLevel = 0;
        uint16_t argCount = 0;
        StaticLookupContext* staticContext = nullptr;
        ValueArray* argsAndWithArgs = nullptr;

        void clearFields() {
            *this = {};
        }
        bool valid() const { return (bool)decl; }
        bool dependentIn(uint32_t nestLevel) const {
            VERIFY(dependentNestLevel <= nestLevel);
            return dependentNestLevel != 0 && dependentNestLevel == nestLevel;
        }
        bool dependentAtAll() const { return dependentNestLevel > 0; }

        std::span<Value> args() const {
            if (!argsAndWithArgs)
                return {};
            return argsAndWithArgs->array().subspan(0, argCount);
        }
        std::span<Value> withArgs() const {
            if (!argsAndWithArgs)
                return {};
            return argsAndWithArgs->array().subspan(argCount);
        }
    };
    struct CompleteDecl : CompleteDeclBase {
        CompleteDecl() = default;
        CompleteDecl(Ptr<Decl> decl, StaticLookupContext* staticContext = nullptr, uint16_t dependentNestLevel = 0)
            : CompleteDeclBase { .decl = decl, .dependentNestLevel = dependentNestLevel, .staticContext = staticContext } { }
        CompleteDecl(const CompleteDecl& other)
            : CompleteDeclBase(other) { ValueArray::ref(argsAndWithArgs); }
        CompleteDecl(CompleteDecl&& other)
            : CompleteDeclBase(other) { other.clearFields(); }
        CompleteDecl& operator=(const CompleteDecl& other) {
            ValueArray::deref(argsAndWithArgs);
            *(CompleteDeclBase*)this = other;
            ValueArray::ref(argsAndWithArgs);
            return *this;
        }
        CompleteDecl& operator=(CompleteDecl&& other) {
            ValueArray::deref(argsAndWithArgs);
            *(CompleteDeclBase*)this = other;
            other.clearFields();
            return *this;
        }
        ~CompleteDecl() {
            ValueArray::deref(argsAndWithArgs);
        }

        void allocateArgs(uint32_t argCount, uint32_t withArgCount = 0) {
            ValueArray::deref(argsAndWithArgs);
            argsAndWithArgs = ValueArray::make(argCount + withArgCount);
            this->argCount = argCount;
        }
    };
    struct ParameterizedDecl : CompleteDecl {
        using CompleteDecl::CompleteDecl;
        ParameterizedDecl(const CompleteDecl& decl)
            : CompleteDecl(decl) { }
        std::span<PositionalValue> args() const {
            static_assert(sizeof(PositionalValue) == sizeof(Value));
            auto args = CompleteDecl::args();
            return { (PositionalValue*)args.data(), args.size() };
        }

    private:
        using CompleteDecl::withArgs;
    };
    struct Type : CompleteDecl {
        using CompleteDecl::CompleteDecl;
        Type(CompleteDecl decl)
            : CompleteDecl(std::move(decl)) { }
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
            ValueArray* array;
            int64_t builtinValue;
            Ptr<Decl> declType;
            struct {
                Ptr<LocalDecl> decl;
                uint32_t nestLevel;
            } dependent;
        } u { .array = nullptr };
        ValueKind kind = ValueKind::Invalid;
        union {
            Word name;
            uint32_t index;
        } id { .name = {} };

        Value() = default;
        Value(Type type, int64_t value)
            : type(std::move(type)), u { .builtinValue = value }, kind(ValueKind::Builtin) { }
        Value(complete_t, Ptr<Decl> type, CompleteDecl decl)
            : type(std::move(decl)), u { .declType = type }, kind(ValueKind::CompleteDecl) { }
        Value(Type type, Ptr<LocalDecl> depDecl, uint32_t level)
            : type(std::move(type)), u { .dependent = { depDecl, level } }, kind(ValueKind::Dependent) { }
        Value(Type type, uint32_t memberCount)
            : type(std::move(type))
            , u { .array = ValueArray::make(memberCount) }
            , kind(ValueKind::Struct) { }
        Value(parameterized_t, Ptr<Decl> type, ParameterizedDecl decl)
            : type(std::move(decl)), u { .declType = type }, kind(ValueKind::ParameterizedDecl) { }

        Value(const Value& other)
            : type(other.type), u(other.u), kind(other.kind), id(other.id) {
            ref();
        }
        Value& operator=(const Value& other) {
            deref();
            type = other.type;
            u = other.u;
            kind = other.kind;
            id = other.id;
            ref();
            return *this;
        }
        Value(Value&& other)
            : type(std::move(other.type)), u(other.u), kind(other.kind), id(other.id) {
            other.kind = ValueKind::Invalid;
        }
        Value& operator=(Value&& other) {
            deref();
            type = std::move(other.type);
            u = other.u;
            kind = other.kind;
            id = other.id;
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
            if (kind == ValueKind::Struct)
                ValueArray::ref(u.array);
        }
        void deref() {
            if (kind == ValueKind::Struct)
                ValueArray::deref(u.array);
        }
        ~Value() {
            deref();
        }
    };
    struct PositionalValue : Value {
        PositionalValue() = default;
        PositionalValue(Value value, uint32_t index)
            : Value(std::move(value)) { id.index = index; }

        uint32_t index() const { return id.index; }
    };
    struct NamedValue : Value {
        NamedValue() = default;
        NamedValue(Value value, Word name)
            : Value(std::move(value)) { id.name = name; }

        Word name() const { return id.name; }
    };

    struct HomogeneousDeclSet {
        std::vector<ParameterizedDecl> decls;
        DeclKind declKind = DeclKind::Invalid;
    };

    enum class LookupContextKind {
        Static,
        Local,
    };
    struct LookupContext {
        LookupContext* parent = nullptr;
        LookupContextKind kind;
        std::span<const Ptr<Decl>> decls = {};
        LookupContext(LookupContextKind kind, LookupContext* parent)
            : parent(parent), kind(kind) { }
    };
    struct LocalLookupContext : LookupContext {
        LocalLookupContext(LookupContext* parent)
            : LookupContext(LookupContextKind::Local, parent) { }
        std::span<Value> values = {};
        LocalLookupContext(const LocalLookupContext&) = delete;
        LocalLookupContext(LocalLookupContext&&) = default;
    };
    struct StaticLookupContext : LookupContext {
        struct DeclValue {
            CompleteDecl decl;
            Value value;
        };
        CompleteDecl staticDecl;

        StaticLookupContext(StaticLookupContext* parent, CompleteDecl staticDecl)
            : LookupContext(LookupContextKind::Static, parent), staticDecl(std::move(staticDecl)) { }

        std::vector<DeclValue> values;
    };
    struct ParameterLookupContext : LocalLookupContext {
        std::vector<Value> valuesVector;

        ParameterLookupContext(LookupContext* parent)
            : LocalLookupContext(parent) { }
        void appendValue(Value value) {
            valuesVector.emplace_back(std::move(value));
            values = valuesVector;
        }
    };
    struct BlockLookupContext : LocalLookupContext {
        std::vector<Ptr<Decl>> declsVector;
        std::vector<Value> valuesVector;
        BlockLookupContext(LookupContext* parent)
            : LocalLookupContext(parent) { }
        void declare(Ptr<Decl> decl, Value value) {
            EXPECT_EQ(declsVector.size(), valuesVector.size());
            declsVector.push_back(decl);
            valuesVector.emplace_back(std::move(value));
            decls = declsVector;
            values = valuesVector;
        }
    };
    struct StructLookupContext : LocalLookupContext {
    };
    StaticLookupContext* asStaticContext(LookupContext& context) {
        if (context.kind == LookupContextKind::Static)
            return static_cast<StaticLookupContext*>(&context);
        return nullptr;
    }

    struct LValue {
        LookupContext* context = nullptr;
        CompleteDecl decl = {};

        bool valid() const {
            return context != nullptr;
        }
    };

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
    Type overloadType;
    Type overloadSetType;
    Type typeOverloadType;
    Type typeOverloadSetType;
    Type boolType;

    // convert{To}(from)
    std::vector<Ptr<FnDecl>> conversions;

    uint32_t dependentNestLevel = 0;
    struct Deduction {
        Ptr<LocalDecl> param;
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
            parseDecl(d, DeclParseScope::Namespace);
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
        numType = completeDecl(findDeclHelper("num"));
        typeType = completeDecl(findDeclHelper("Type"));
        overloadType = completeDecl(findDeclHelper("Overload"));
        overloadSetType = completeDecl(findDeclHelper("OverloadSet"));
        typeOverloadType = completeDecl(findDeclHelper("TypeOverload"));
        typeOverloadSetType = completeDecl(findDeclHelper("TypeOverloadSet"));

        boolType = completeDecl(findDeclHelper("bool"));
        auto falseDecl = completeDecl(findDeclHelper("false"));
        VERIFY(at(falseDecl.decl).kind == DeclKind::GlobalDecl);
        falseDecl.staticContext->values.push_back({ falseDecl, makeBuiltinValue(boolType, 0) });
        auto trueDecl = completeDecl(findDeclHelper("true"));
        VERIFY(at(trueDecl.decl).kind == DeclKind::GlobalDecl);
        trueDecl.staticContext->values.push_back({ trueDecl, makeBuiltinValue(boolType, 1) });

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
            Type ret = { (Ptr<Decl>)value.u.dependent.decl };
            ret.dependentNestLevel = value.u.dependent.nestLevel;
            return ret;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    Type toCompleteType(const Value& in) {
        Type inType = typeOf(in);
        if (cmpCompleteDecls(inType, typeType)) {
            return asTypeValue(in);
        }
        if (cmpCompleteDecls(inType, typeOverloadSetType)) {
            VERIFY(in.kind == ValueKind::Struct);
            if (in.u.array->size != 1)
                return {};
            Value parametricT = in.u.array->array()[0];
            VERIFY(parametricT.kind == ValueKind::ParameterizedDecl);
            return completeDecl(parametricT.type);
        }
        return {};
    }

    Value makeBuiltinValue(Type type, int64_t value) { return { std::move(type), value }; }
    Value makeDependentValue(Ptr<LocalDecl> decl, Type type, uint32_t nestLevel) {
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
            EXPECT_EQ(l.u.array->size, r.u.array->size);
            uint32_t size = l.u.array->size;
            for (uint32_t i = 0; i < size; i++) {
                if (!cmpValue(l.u.array->array()[i], r.u.array->array()[i]))
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
        EXPECT_EQ(l.args().size(), r.args().size());
        for (uint32_t i = 0; i < l.args().size(); i++) {
            if (!cmpValue(l.args()[i], r.args()[i]))
                return false;
        }
        return true;
    }

    bool positionArguments(std::span<PositionalValue> out, Parameters& parameters, std::span<const PositionalValue> inArgs, std::span<const NamedValue> subArgs) {
        auto params = at(parameters.params);
        uint32_t inOff = 0;
        uint32_t subOff = 0;
        uint32_t outOff = 0;
        for (uint32_t i = 0; i < params.size(); i++) {
            if (inOff < inArgs.size() && inArgs[inOff].index() == i) {
                out[outOff++] = inArgs[inOff];
                inOff += 1;
            } else if (subOff < subArgs.size() && (!subArgs[subOff].name() || cmpWord(at(params[i]).name, subArgs[subOff].name()))) {
                out[outOff++] = { subArgs[subOff], i };
                subOff += 1;
            }
        }
        return subOff == subArgs.size();
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
            deductions.push_back({ (Ptr<LocalDecl>)target.decl, makeTypeValue(source) });
            return true;
        }
        if (target.dependentAtAll()) {
            // allow { Q: Type, a: Q, A: Type, c: constant{A, a} }
            return true;
        }
        if (source.decl != target.decl)
            return false;
        VERIFY(source.args().size() == target.args().size());
        for (uint32_t i = 0; i < source.args().size(); i++) {
            if (!deduceParameters(source.args()[i], target.args()[i], deductions))
                return false;
        }

        return true;
    }
    Value convertAndDeduce(Value targetTypeValue, const Value& sourceValue, std::vector<Deduction>& deductions) {
        if (cmpCompleteDecls(typeOf(targetTypeValue), typeOverloadSetType)) {
            auto completeType = toCompleteType(targetTypeValue);
            if (!completeType.valid())
                return {};
            targetTypeValue = makeTypeValue(completeType);
        }
        if (cmpCompleteDecls(typeOf(targetTypeValue), typeType)) {
            // FIXME: merge this case with deduceParameters(Type, Type)
            Type targetType = asTypeValue(targetTypeValue);
            Type sourceType = typeOf(sourceValue);
            if (targetType.dependentIn(this->dependentNestLevel)) {
                deductions.push_back({ (Ptr<LocalDecl>)targetType.decl, makeTypeValue(sourceType) });
                return sourceValue;
            }
            if (targetType.dependentAtAll()) {
                // deduction will be handeled at a lower nest level
                return sourceValue;
            }
            if (targetType.decl == sourceType.decl) {
                for (uint32_t i = 0; i < sourceType.args().size(); i++) {
                    if (!deduceParameters(sourceType.args()[i], targetType.args()[i], deductions))
                        return {};
                }
                return sourceValue;
            }

            // TODO: should this be a conversion?
            if (cmpCompleteDecls(sourceType, typeOverloadSetType)) {
                return makeTypeValue(toCompleteType(sourceValue));
            }
        }

        std::optional<CompleteCall> call;
        for (Ptr<FnDecl> declP : conversions) {
            ParameterizedDecl decl { (Ptr<Decl>)declP, globalContext };
            decl.allocateArgs(1);
            decl.args()[0] = { targetTypeValue, 0 };
            std::array<PositionalValue, 1> fnArgs { { { sourceValue, 0 } } };
            auto c = completeCall(decl, fnArgs);
            if (c.has_value()) {
                VERIFY(!call.has_value());
                call = std::move(c.value());
            }
        }
        if (!call.has_value())
            return {};
        return evaluateFunction(call.value());
    }

    std::optional<ParameterLookupContext> makeParameterContext(
        LookupContext& parent, const Parameters& params, std::span<const PositionalValue> args, std::vector<Deduction>& deductions) {

        std::span<Ptr<Decl>> allDecls = at((Span<Ptr<Decl>>)params.params);
        ParameterLookupContext context { &parent };
        uint32_t argOff = 0;

        for (uint32_t i = 0; i < allDecls.size(); i++) {
            Value arg;
            Value type = evaluateExpr(context, as<LocalDecl>(allDecls[i]).type);
            if (argOff < args.size() && i == args[argOff].index()) {
                arg = convertAndDeduce(type, args[argOff], deductions);
                if (!arg.valid()) {
                    fmt::print("unable to initialize ");
                    dumpValue(type);
                    fmt::print("with ");
                    dumpValue(args[argOff]);
                    return {};
                }
                argOff += 1;
            } else {
                auto completeType = toCompleteType(type);
                if (!completeType.valid())
                    return {};
                arg = makeDependentValue((Ptr<LocalDecl>)allDecls[i], completeType, this->dependentNestLevel);
            }
            context.decls = allDecls.subspan(0, i + 1);
            context.appendValue(arg);
        }
        EXPECT_EQ(argOff, args.size());

        return context;
    }
    void applyDeductions(LocalLookupContext& context, std::span<const Deduction> deductions) {
        for (uint32_t i = 0; i < context.decls.size(); i++) {
            auto decl = context.decls[i];
            auto& value = context.values[i];
            for (auto& deduc : deductions) {
                if (decl != (Ptr<Decl>)deduc.param)
                    continue;

                if (value.dependentIn(this->dependentNestLevel)) {
                    // value was deduced
                    value = deduc.value;
                } else if (!cmpValue(value, deduc.value)) {
                    // value was deduced multiple times but not to the same value
                    value = {};
                }
            }
        }
    }
    bool completeParameterContext(std::span<Value> out, LocalLookupContext& context) {
        for (uint32_t i = 0; i < context.decls.size(); i++) {
            Ptr<LocalDecl> decl = (Ptr<LocalDecl>)context.decls[i];
            auto& value = context.values[i];
            if (!value.valid())
                return false;

            Value arg;
            if (value.dependentIn(this->dependentNestLevel)) {
                // no argument was provided and nothing was deduced
                // -> use the default arugment
                auto& param = at(decl);
                if (!param.initializer)
                    return false;
                arg = evaluateExpr(context, param.initializer);
                if (param.type) {
                    auto type = toCompleteType(evaluateExpr(context, param.type));
                    if (!type.valid())
                        return false;
                    arg = convert(context, type, arg);
                }
            } else {
                // argument shoud already be converted
                arg = value;
            }

            out[i] = std::move(arg);
        }
        return true;
    }
    CompleteDecl completeDecl(const ParameterizedDecl& decl) {
        DependentScope depScope { this };

        Ptr<StaticDecl> sDecl = asStaticDecl(decl.decl);
        if (!sDecl) {
            VERIFY(!decl.staticContext);
            return CompleteDecl { decl.decl, nullptr };
        }
        VERIFY((bool)decl.staticContext);
        // fmt::println("completing '{}' at level {}", sview(at(sDecl).name), depScope.level);

        auto withCtx = makeParameterContext(*decl.staticContext, at(sDecl).with.params, {}, depScope.deductions);
        if (!withCtx.has_value())
            return {};

        auto paramCtx = makeParameterContext(withCtx.value(), at(sDecl).parametric, decl.args(), depScope.deductions);
        if (!paramCtx.has_value())
            return {};

        applyDeductions(withCtx.value(), depScope.deductions);
        applyDeductions(paramCtx.value(), depScope.deductions);

        CompleteDecl out { sDecl, decl.staticContext };
        out.allocateArgs(paramCtx.value().decls.size(), withCtx.value().decls.size());

        if (!completeParameterContext(out.withArgs(), withCtx.value()))
            return {};

        if (!completeParameterContext(out.args(), paramCtx.value()))
            return {};

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
            if (!cmpWord(at(decl).name, name))
                continue;

            out.context = &context;
            ParameterizedDecl parameterized { decl, asStaticContext(context) };
            if (context.kind == LookupContextKind::Static) {
                Ptr<StaticDecl> sDecl = asStaticDecl(decl);
                VERIFY((bool)sDecl);
                parameterized.allocateArgs(args.size());
                if (!positionArguments(parameterized.args(), at(sDecl).parametric, {}, args))
                    continue;
            }

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
            if (r.valid())
                return r;

            context = context->parent;
        }
        fmt::println("looking up '{}' failed", sview(ident.word));
        VERIFY_NOT_REACHED();
    }
    Value& getValueRef(const LValue& lVal) {
        VERIFY(lVal.valid());
        VERIFY((bool)asVar(lVal.decl.decl));
        if (auto sContext = asStaticContext(*lVal.context)) {
            for (auto& v : sContext->values) {
                if (cmpCompleteDecls(v.decl, lVal.decl))
                    return v.value;
            }
            Value v = initialize(*lVal.context, lVal.decl);
            sContext->values.push_back({ lVal.decl, v });
            return sContext->values.back().value;
        }

        LocalLookupContext* lContext = (LocalLookupContext*)lVal.context;
        EXPECT_EQ(lContext->decls.size(), lContext->values.size());
        for (uint32_t i = 0; i < lContext->decls.size(); i++) {
            if (lContext->decls[i] == lVal.decl.decl)
                return lContext->values[i];
        }
        VERIFY_NOT_REACHED();
    }
    Value getValue(const LValue& lVal) {
        return getValueRef(lVal);
    }
    void setValue(const LValue& lVal, Value value) {
        getValueRef(lVal) = value;
    }
    
    // args must not be a temporary and the values inside it may be modified
    LocalLookupContext makeCompleteParameterContext(LookupContext& parent, const Parameters& params, std::span<Value> args) {
        auto decls = at((Span<Ptr<Decl>>)params.params);
        LocalLookupContext context { &parent };
        context.decls = decls;
        context.values = args;
        return context;
    }
    Value initialize(LookupContext& parent, const CompleteDecl& decl) {
        VERIFY(at(decl.decl).kind == DeclKind::GlobalDecl);
        const GlobalDecl& d = as<GlobalDecl>(decl.decl);
        LocalLookupContext withCtx = makeCompleteParameterContext(parent, d.with.params, decl.withArgs());
        LocalLookupContext paramCtx = makeCompleteParameterContext(withCtx, d.parametric, decl.args());

        Value source = evaluateExpr(paramCtx, d.initializer);
        if (d.type) {
            Value v = evaluateExpr(paramCtx, d.type);
            VERIFY(v.kind == ValueKind::CompleteDecl);
            VERIFY(v.u.declType == typeType.decl);
            source = convert(paramCtx, toCompleteType(v), source);
        }
        return source;
    }
    LValue toLValue(const LookupResult& result) {
        VERIFY(result.declKind == DeclKind::LocalDecl || result.declKind == DeclKind::GlobalDecl);
        CompleteDecl theDecl;
        for (const ParameterizedDecl& d : result.decls) {
            auto complete = completeDecl(d);
            if (complete.valid()) {
                if (theDecl.valid())
                    return {};
                theDecl = std::move(complete);
            }
        }
        if (theDecl.valid())
            return { result.context, theDecl };
        return {};
    }

    Value convert(LookupContext&, Type, Value val) {
        return val;
    }

    Value evaluateExpr(LookupContext& ctx, Ptr<Expr> p) {
        auto& e = at(p);

        Value ret;
#define EXPR_KIND(kind)  \
    case ExprKind::kind: \
        ret = eval##kind(ctx, (kind&)e); \
        break;

        switch (e.kind) {
            ENUMERATE_EXPR_KINDS
        default:
            VERIFY_NOT_REACHED();
        }
#undef EXPR_KIND

        if (!ret.valid())
            fmt::println("evaluating {} was invalid", toString(e.kind));
        return ret;
    }

    Value evalIdentifierExpr(LookupContext& ctx, IdentifierExpr& e) {
        LookupResult r = lookupIdentifier(ctx, e.identifier);

        switch (r.declKind) {
        case DeclKind::GlobalDecl:
        case DeclKind::LocalDecl: {
            LValue lVal = toLValue(r);
            if (lVal.valid())
                return getValue(lVal);
            return {};
        }
        case DeclKind::FnDecl:
        case DeclKind::MethodDecl:
        case DeclKind::StructDecl: {
            const auto& setType = r.declKind == DeclKind::StructDecl ? typeOverloadSetType : overloadSetType;
            const auto& itemType = r.declKind == DeclKind::StructDecl ? typeOverloadType : overloadType;
            Value v = makeStructValue(setType, r.decls.size());
            for (uint32_t i = 0; i < r.decls.size(); i++)
                v.u.array->array()[i] = makeParameterizedDeclValue(itemType.decl, std::move(r.decls[i]));
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
        std::vector<Value> args = {};
    };
    std::optional<CompleteCall> completeCall(const ParameterizedDecl& fnDecl, std::span<const NamedValue> namedFnArgs) {
        std::vector<PositionalValue> posFnArgs;
        posFnArgs.resize(namedFnArgs.size());
        if (!positionArguments(posFnArgs, as<FnDecl>(fnDecl.decl).params, {}, namedFnArgs))
            return {};

        return completeCall(fnDecl, posFnArgs);
    }
    std::optional<CompleteCall> completeCall(const ParameterizedDecl& fnDecl, std::span<const PositionalValue> posFnArgs) {
        VERIFY(fnDecl.staticContext != nullptr);
        auto& fn = as<FnDecl>(fnDecl.decl);

        DependentScope depScope { this };

        auto withCtx = makeParameterContext(*fnDecl.staticContext, fn.with.params, {}, depScope.deductions);
        if (!withCtx.has_value())
            return {};

        auto parametricCtx = makeParameterContext(withCtx.value(), fn.parametric, fnDecl.args(), depScope.deductions);
        if (!parametricCtx.has_value())
            return {};

        auto fnCtx = makeParameterContext(parametricCtx.value(), fn.params, posFnArgs, depScope.deductions);
        if (!fnCtx.has_value())
            return {};

        applyDeductions(withCtx.value(), depScope.deductions);
        applyDeductions(parametricCtx.value(), depScope.deductions);
        applyDeductions(fnCtx.value(), depScope.deductions);

        CompleteCall out { { fnDecl.decl, fnDecl.staticContext } };
        out.target.allocateArgs(parametricCtx.value().decls.size(), withCtx.value().decls.size());
        out.args.resize(fnCtx.value().decls.size());

        if (!completeParameterContext(out.target.withArgs(), withCtx.value()))
            return {};

        if (!completeParameterContext(out.target.args(), parametricCtx.value()))
            return {};

        if (!completeParameterContext(out.args, fnCtx.value()))
            return {};

        return out;
    }
    Value evaluateFunction(CompleteCall call) {
        auto& targetDecl = as<FnDecl>(call.target.decl);
        LocalLookupContext withCtx = makeCompleteParameterContext(*call.target.staticContext, targetDecl.with.params, call.target.withArgs());
        LocalLookupContext parametricCtx = makeCompleteParameterContext(withCtx, targetDecl.parametric, call.target.args());
        LocalLookupContext fnParmsCtx = makeCompleteParameterContext(parametricCtx, targetDecl.params, call.args);
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
        if (cmpCompleteDecls(baseType, typeOverloadSetType)) {
            base = makeTypeValue(toCompleteType(base));
            baseType = typeOf(base);
        }
        if (cmpCompleteDecls(baseType, typeType)) {
            // hacked constructor: make builtin value
            VERIFY(e.args.args.count == 0);
            return { toCompleteType(base), (int64_t)0 };
        } else if (cmpCompleteDecls(baseType, overloadSetType)) {
            auto args = evaluateArguments(ctx, e.args);
            VERIFY(base.kind == ValueKind::Struct);
            std::optional<CompleteCall> call;
            for (Value& overload : base.u.array->array()) {
                VERIFY(overload.kind == ValueKind::ParameterizedDecl);
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

    void setExprValue(LookupContext& context, Ptr<Expr> p, Value value) {
        auto& e = at(p);
        switch (e.kind) {
        case ExprKind::IdentifierExpr: {
            auto& idExpr = (IdentifierExpr&)e;
            LookupResult r = lookupIdentifier(context, idExpr.identifier);
            setValue(toLValue(r), std::move(value));
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    ControlFlow evaluateStmt(BlockLookupContext& ctx, Ptr<Stmt> p) {
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
        BlockLookupContext context { &parent };
        for (Ptr<Stmt> stmt : at(body.body)) {
            PROPEGATE_FLOW(evaluateStmt(context, stmt));
        }
        return {};
    }

    ControlFlow evalLetStmt(BlockLookupContext& context, LetStmt& stmt) {
        VarInfo& info = at(asVar(stmt.decl));
        Value init = evaluateExpr(context, info.initializer);
        if (info.type) {
            Type type = toCompleteType(evaluateExpr(context, info.type));
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
            BlockLookupContext localCtx { &context };
            PROPEGATE_FLOW(evaluateStmt(localCtx, stmt.ifTrue));
        } else if (stmt.ifFalse) {
            BlockLookupContext localCtx { &context };
            PROPEGATE_FLOW(evaluateStmt(localCtx, stmt.ifFalse));
        }
        return {};
    }

    ControlFlow evalNullStmt(LookupContext&, NullStmt&) { return {}; }

    ControlFlow evalAssignStmt(LookupContext& context, AssignStmt& stmt) {
        if (stmt.op == AssignOperator::None) {
            Value right = evaluateExpr(context, stmt.right);
            setExprValue(context, stmt.left, std::move(right));
        } else
            VERIFY_NOT_REACHED();
        return {};
    }

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
            if (auto sDecl = asStaticDecl(value.type.decl)) {
                for (uint32_t i = 0; i < value.type.args().size(); i++) {
                    fmt::print("  '{}': ", sview(at(at(at(sDecl).parametric.params, i)).name));
                    dumpValue(value.type.args()[i]);
                }
            }
            break;
        case ValueKind::Dependent:
            fmt::println("[{}] dependend {}", sview(at(value.type.decl).name), sview(at(value.u.dependent.decl).name));
            break;
        case ValueKind::Struct:
            fmt::println("[{}] struct with {} members", sview(at(value.type.decl).name), value.u.array->size);
            for (const Value& member : value.u.array->array()) {
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

        struct TypeOverload{} {}
        struct TypeOverloadSet{} {}

        struct bool{} {}
        const true: bool = {};
        const false: bool = {};
    )str");
    it.findBuiltins();
    it.interpretDecls(R"str(
        function foo(x: bool) {
            if x
                x = foo(false);
            return x;
        }

        struct constant{T: Type, v: T} {}
        with{T: Type}
        function mkConst{v: T}() { return constant{T, v}(); }

        function bar{a: num, A: Type}(b: constant{A, a}) { return a; }
        function bar{a: num, A: Type, b: constant{A, a}, B: Type}(c: constant{B, b}) { return a; }
        function bar{a: num, A: Type, b: constant{A, a}, B: Type, c: constant{B, b}, C: Type}(d: constant{C, c}) { return a; }
    )str");

    auto eval = [&](const char* expr) {
        Interpreter::Value v = it.interpretExpr(expr);
        fmt::print("eval: ");
        it.dumpValue(v);
    };
    eval("foo(true)");
    eval("bar(mkConst{mkConst{5}()}())");
}