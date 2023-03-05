#include "Parser.h"
#include <algorithm>
#include <optional>
#include <vector>

struct Interpreter : STContext {
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
        Array,
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
        Value(Type type, uint32_t size)
            : type(std::move(type))
            , u { .array = ValueArray::make(size) }
            , kind(ValueKind::Array) { }
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
            if (kind == ValueKind::Array)
                ValueArray::ref(u.array);
        }
        void deref() {
            if (kind == ValueKind::Array)
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

#define EXPR_KIND(kind) kind,
    enum class RecordKind {
        Conversion,
        Empty,
        Hacked,
        ENUMERATE_EXPR_KINDS
    };
#undef EXPR_KIND
    const char* toString(RecordKind kind) {
#define EXPR_KIND(kind)    \
    case RecordKind::kind: \
        return #kind;

        switch (kind) {
            ENUMERATE_EXPR_KINDS
        case RecordKind::Conversion:
            return "Conversion";
        case RecordKind::Empty:
            return "Empty";
        case RecordKind::Hacked:
            return "Hacked";
        default:
            return "????";
        }

#undef EXPR_KIND
    }

    struct ExprRecord {
        RecordKind kind;
        uint32_t refCnt = 1;
        Value result;

        ExprRecord(RecordKind kind, Value result)
            : kind(kind), result(std::move(result)) { }
    };
    struct ExprResult {
        ExprRecord* ptr;

        template<std::derived_from<ExprRecord> T, typename... Args>
        static ExprResult make(Args&&... args) {
            return ExprResult { new T(args...) };
        }

        void ref() {
            ptr->refCnt += 1;
        }
        void deref() {
            ptr->refCnt -= 1;
            if (ptr->refCnt == 0)
                delete ptr;
        }

        Value value() const { return ptr->result; }
        operator Value() const { return value(); }
        RecordKind kind() const { return ptr->kind; }
        template<typename T>
        T& as() { return *(T*)ptr; }

        ExprResult(const ExprResult& other)
            : ptr(other.ptr) { ref(); }
        ExprResult& operator=(const ExprResult& other) {
            deref();
            ptr = other.ptr;
            ref();
            return *this;
        }
        ~ExprResult() {
            deref();
        }

    private:
        ExprResult(ExprRecord* ptr)
            : ptr(ptr) { }
    };
    struct PositionalExprResult : ExprResult {
        uint32_t m_index = 0;
        PositionalExprResult(ExprResult result, uint32_t index)
            : ExprResult(result), m_index(index) { }
        PositionalValue value() const {
            return { ExprResult::value(), index() };
        }
        operator PositionalValue() const { return value(); }
        uint32_t index() const { return m_index; }
    };
    struct NamedExprResult : ExprResult {
        Word m_name = {};
        NamedExprResult(ExprResult result, Word name)
            : ExprResult(result), m_name(name) { }
        NamedValue value() const {
            return { ExprResult::value(), name() };
        }
        operator NamedValue() const { return value(); }
        Word name() const { return m_name; }
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

    Interpreter()
        : STContext(STContext::create()) { }

    void interpretDecls(SourceBuffer buffer) {
        Parser parser { *this, buffer };
        auto decls = parser.beginSpan<Ptr<Decl>>();
        while (parser.tok.kind() != TokenKind::EOS) {
            auto& d = parser.append(decls, {});
            parser.parseDecl(d, Parser::DeclParseScope::Namespace);
        }
        Ptr<StaticLookupContext> ctx = make<StaticLookupContext>(globalContext, CompleteDecl {});
        globalContext = &at(ctx);
        globalContext->decls = at(parser.finalizeSpan(decls));
    }
    Value interpretExpr(SourceBuffer buffer) {
        Parser parser { *this, buffer };
        Ptr<Expr> e;
        parser.parseBinaryExpr(e);
        return evaluateExpr(*globalContext, e);
    }

    ParameterizedDecl findDeclHelper(Word name) {
        Identifier id { {}, name };
        LookupResult r = lookupIdentifier(*globalContext, id);
        EXPECT_EQ(r.decls.size(), 1u);
        return r.decls[0];
    }
    CompleteDecl findCompDeclHelper(Word name) {
        return completeDecl(findDeclHelper(name));
    }
    void findBuiltins() {
        numType = findCompDeclHelper(asWord("num"));
        typeType = findCompDeclHelper(asWord("Type"));
        overloadType = findCompDeclHelper(asWord("Overload"));
        overloadSetType = findCompDeclHelper(asWord("OverloadSet"));
        typeOverloadType = findCompDeclHelper(asWord("TypeOverload"));
        typeOverloadSetType = findCompDeclHelper(asWord("TypeOverloadSet"));

        boolType = findCompDeclHelper(asWord("bool"));
        auto falseDecl = findCompDeclHelper(asWord("false"));
        VERIFY(at(falseDecl.decl).kind == DeclKind::GlobalDecl);
        falseDecl.staticContext->values.push_back({ falseDecl, makeBuiltinValue(boolType, 0) });
        auto trueDecl = findCompDeclHelper(asWord("true"));
        VERIFY(at(trueDecl.decl).kind == DeclKind::GlobalDecl);
        trueDecl.staticContext->values.push_back({ trueDecl, makeBuiltinValue(boolType, 1) });

        arrayDecl = findDeclHelper(asWord("Array")).decl;
        VERIFY(at(arrayDecl).kind == DeclKind::StructDecl);
    }

    Type typeOf(const Value& value) {
        switch (value.kind) {
        case ValueKind::Array:
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
            VERIFY(in.kind == ValueKind::Array);
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
    Value makeArrayValue(Type type, uint32_t size) {
        return Value { std::move(type), size };
    }
    Value makeParameterizedDeclValue(Ptr<Decl> type, ParameterizedDecl decl) {
        return Value { parameterized_t(), type, std::move(decl) };
    }

    bool cmpWord(Word l, Word r) { return l.id == r.id; }
    bool cmpValue(const Value& l, const Value& r) {
        VERIFY(l.kind == r.kind);
        switch (l.kind) {
        case ValueKind::Builtin:
            VERIFY(cmpCompleteDecls(l.type, r.type));
            return l.u.builtinValue == r.u.builtinValue;
        case ValueKind::Array: {
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

    std::optional<std::vector<PositionalExprResult>> positionArguments(Parameters& parameters,
        std::span<const PositionalExprResult> inArgs, std::span<const NamedExprResult> subArgs) {

        std::vector<PositionalExprResult> out;
        auto params = at(parameters.params);
        uint32_t inOff = 0;
        uint32_t subOff = 0;
        for (uint32_t i = 0; i < params.size(); i++) {
            if (inOff < inArgs.size() && inArgs[inOff].index() == i) {
                out.push_back(inArgs[inOff]);
                inOff += 1;
            } else if (subOff < subArgs.size() && (!subArgs[subOff].name() || cmpWord(at(params[i]).name, subArgs[subOff].name()))) {
                out.push_back({ subArgs[subOff], i });
                subOff += 1;
            }
        }
        if (subOff != subArgs.size())
            return {};
        return out;
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
    struct ConversionRecord : ExprRecord {
        ExprResult base;
        ConversionRecord(Value result, ExprResult base)
            : ExprRecord(RecordKind::Conversion, result), base(base) { }
    };
    ExprResult convertAndDeduce(Value targetTypeValue, ExprResult sourceValue, std::vector<Deduction>& deductions) {
        if (cmpCompleteDecls(typeOf(targetTypeValue), typeOverloadSetType)) {
            auto completeType = toCompleteType(targetTypeValue);
            if (!completeType.valid())
                return ExprResult::make<ConversionRecord>(Value {}, sourceValue);
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
                        return ExprResult::make<ConversionRecord>(Value {}, sourceValue);
                }
                return sourceValue;
            }

            // TODO: should this be a conversion?
            if (cmpCompleteDecls(sourceType, typeOverloadSetType)) {
                return ExprResult::make<ConversionRecord>(makeTypeValue(toCompleteType(sourceValue)), sourceValue);
            }
        }

        std::optional<CompleteCall> call;
        for (Ptr<FnDecl> declP : conversions) {
            ParameterizedDecl decl { (Ptr<Decl>)declP, globalContext };
            decl.allocateArgs(1);
            decl.args()[0] = { targetTypeValue, 0 };
            std::vector<PositionalExprResult> fnArgs { { sourceValue, 0 } };
            auto c = completeCall(decl, std::move(fnArgs));
            if (c.has_value()) {
                VERIFY(!call.has_value());
                call = std::move(c.value());
            }
        }
        if (!call.has_value())
            return ExprResult::make<ConversionRecord>(Value {}, sourceValue);
        return ExprResult::make<ConversionRecord>(evaluateFunction(call.value()), sourceValue);
    }

    struct HackedRecord : ExprRecord {
        HackedRecord(Value result)
            : ExprRecord(RecordKind::Hacked, std::move(result)) { }
    };
    bool convertFnArgs(LookupContext& context, const Parameters& params, std::span<PositionalExprResult> args, std::vector<Deduction>& deductions) {
        for (auto& arg : args) {
            Value type = evaluateExpr(context, at(at(params.params, arg.index())).type);
            ExprResult cvtArg = convertAndDeduce(type, arg, deductions);
            if (!cvtArg.value().valid()) {
                fmt::print("unable to initialize ");
                dumpValue(type);
                fmt::print("with ");
                dumpValue(arg);
                return false;
            }
            arg = { cvtArg, arg.index() };
        }
        return true;
    }

    struct FnArgumentResult : ExprResult {
        bool isInOut = false;
    };
    std::optional<std::vector<FnArgumentResult>> completeFnArgs(LookupContext& context, const Parameters& params, std::span<const PositionalExprResult> args) {
        std::vector<FnArgumentResult> out;
        uint32_t argOff = 0;
        for (uint32_t i = 0; i < params.params.count; i++) {
            ExprResult arg
                = (argOff < args.size() && i == args[argOff].index())
                ? (ExprResult)args[argOff++]
                : evaluateDefaultArg(context, at(params.params, i));

            if (!arg.value().valid())
                return {};
            out.push_back({ std::move(arg), at(at(params.params, i)).isInOut });
        }
        return out;
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
                arg = convertAndDeduce(type, ExprResult::make<HackedRecord>(args[argOff]), deductions);
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

    struct EmptyRecord : ExprRecord {
        EmptyRecord()
            : ExprRecord(RecordKind::Empty, {}) { }
    };
    ExprResult evaluateDefaultArg(LookupContext& context, Ptr<LocalDecl> decl) {
        auto& param = at(decl);
        if (!param.initializer)
            return ExprResult::make<EmptyRecord>();
        ExprResult arg = evaluateExpr(context, param.initializer);
        if (param.type) {
            auto type = toCompleteType(evaluateExpr(context, param.type));
            if (!type.valid())
                return ExprResult::make<ConversionRecord>(Value {}, arg);
            arg = ExprResult::make<ConversionRecord>(convert(context, type, arg), arg);
        }
        return arg;
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
                arg = evaluateDefaultArg(context, decl);
                if (!arg.valid())
                    return false;
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

    std::vector<NamedExprResult> evaluateArguments(LookupContext& ctx, Arguments& a) {
        std::vector<NamedExprResult> out;
        auto args = at(a.args);
        for (auto& arg : args) {
            out.push_back({ evaluateExpr(ctx, arg.source), arg.target });
        }
        return out;
    }

    LookupResult lookupIdentifierIn(LookupContext& context, Word name, std::span<const NamedExprResult> args) {
        LookupResult out;
        for (Ptr<Decl> decl : context.decls) {
            if (!cmpWord(at(decl).name, name))
                continue;

            out.context = &context;
            ParameterizedDecl parameterized { decl, asStaticContext(context) };
            if (context.kind == LookupContextKind::Static) {
                Ptr<StaticDecl> sDecl = asStaticDecl(decl);
                VERIFY((bool)sDecl);
                auto posArgs = positionArguments(at(sDecl).parametric, {}, args);
                if (!posArgs.has_value())
                    continue;

                parameterized.allocateArgs(args.size());
                for (uint32_t i = 0; i < args.size(); i++)
                    parameterized.args()[i] = posArgs.value()[i];
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
            Value v = initialize(lVal.decl);
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
    template<typename Callback>
    auto withStaticContext(const CompleteDecl& decl, Callback&& callback) {
        auto& d = at(asStaticDecl(decl.decl));
        VERIFY((bool)decl.staticContext);
        LocalLookupContext withCtx = makeCompleteParameterContext(*decl.staticContext, d.with.params, decl.withArgs());
        LocalLookupContext paramCtx = makeCompleteParameterContext(withCtx, d.parametric, decl.args());
        return callback(paramCtx);
    }
    Value initialize(const CompleteDecl& decl) {
        VERIFY(at(decl.decl).kind == DeclKind::GlobalDecl);
        return withStaticContext(decl, [&](LookupContext& context) {
            auto& d = as<GlobalDecl>(decl.decl);
            Value source = evaluateExpr(context, d.initializer);
            if (d.type) {
                Type type = toCompleteType(evaluateExpr(context, d.type));
                VERIFY(type.valid());
                source = convert(context, type, source);
            }
            return source;
        });
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

    ExprResult evaluateExpr(LookupContext& ctx, Ptr<Expr> p) {
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

    struct IdentifierExprRecord : ExprRecord {
        LValue lValue;
        IdentifierExprRecord(Value result, LValue lValue)
            : ExprRecord(RecordKind::IdentifierExpr, result), lValue(lValue) { }
    };
    ExprResult evalIdentifierExpr(LookupContext& ctx, IdentifierExpr& e) {
        LookupResult r = lookupIdentifier(ctx, e.identifier);

        switch (r.declKind) {
        case DeclKind::GlobalDecl:
        case DeclKind::LocalDecl: {
            LValue lVal = toLValue(r);
            if (lVal.valid())
                return ExprResult::make<IdentifierExprRecord>(getValue(lVal), lVal);
            return ExprResult::make<IdentifierExprRecord>(Value {}, lVal);
        }
        case DeclKind::FnDecl:
        case DeclKind::StructDecl: {
            const auto& setType = r.declKind == DeclKind::StructDecl ? typeOverloadSetType : overloadSetType;
            const auto& itemType = r.declKind == DeclKind::StructDecl ? typeOverloadType : overloadType;
            Value v = makeArrayValue(setType, r.decls.size());
            for (uint32_t i = 0; i < r.decls.size(); i++)
                v.u.array->array()[i] = makeParameterizedDeclValue(itemType.decl, std::move(r.decls[i]));
            return ExprResult::make<IdentifierExprRecord>(v, LValue {});
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    struct IntLiteralExprRecord : ExprRecord {
        IntLiteralExprRecord(Value result)
            : ExprRecord(RecordKind::IntLiteralExpr, result) { }
    };
    ExprResult evalIntLiteralExpr(LookupContext&, IntLiteralExpr& e) {
        return ExprResult::make<IntLiteralExprRecord>(makeBuiltinValue(numType, e.value));
    }

    struct CompleteCall {
        CompleteDecl target;
        std::vector<FnArgumentResult> args = {};
    };
    std::optional<CompleteCall> completeCall(const ParameterizedDecl& fnDecl, std::span<const NamedExprResult> namedFnArgs) {
        auto posFnArgs = positionArguments(as<FnDecl>(fnDecl.decl).params, {}, namedFnArgs);
        if (!posFnArgs.has_value())
            return {};

        return completeCall(fnDecl, std::move(posFnArgs.value()));
    }
    std::optional<CompleteCall> completeCall(const ParameterizedDecl& fnDecl, std::vector<PositionalExprResult> posFnArgs) {
        VERIFY(fnDecl.staticContext != nullptr);
        auto& fn = as<CallableDecl>(fnDecl.decl);

        DependentScope depScope { this };

        auto withCtx = makeParameterContext(*fnDecl.staticContext, fn.with.params, {}, depScope.deductions);
        if (!withCtx.has_value())
            return {};

        auto parametricCtx = makeParameterContext(withCtx.value(), fn.parametric, fnDecl.args(), depScope.deductions);
        if (!parametricCtx.has_value())
            return {};

        if (!convertFnArgs(parametricCtx.value(), fn.params, posFnArgs, depScope.deductions))
            return {};

        applyDeductions(withCtx.value(), depScope.deductions);
        applyDeductions(parametricCtx.value(), depScope.deductions);

        CompleteCall out { { fnDecl.decl, fnDecl.staticContext } };
        out.target.allocateArgs(parametricCtx.value().decls.size(), withCtx.value().decls.size());

        if (!completeParameterContext(out.target.withArgs(), withCtx.value()))
            return {};

        if (!completeParameterContext(out.target.args(), parametricCtx.value()))
            return {};

        auto fnArgs = completeFnArgs(parametricCtx.value(), fn.params, posFnArgs);
        VERIFY(fnArgs.has_value());
        if (!fnArgs.has_value())
            return {};
        out.args = std::move(fnArgs.value());

        return out;
    }
    Value evaluateFunction(CompleteCall call) {
        return evaluateFunction(std::move(call), Value {});
    }
    Value evaluateFunction(CompleteCall call, Value assignArg) {
        VERIFY(at(call.target.decl).kind == DeclKind::FnDecl);
        auto& targetDecl = as<FnDecl>(call.target.decl);
        return withStaticContext(call.target, [&](LookupContext& parametricCtx) -> Value {
            ParameterLookupContext fnParamCtx { &parametricCtx };
            fnParamCtx.decls = at((Span<Ptr<Decl>>)targetDecl.params.params);
            for (uint32_t i = 0; i < call.args.size(); i++)
                fnParamCtx.appendValue(call.args[i]);

            BlockLookupContext assignParamCtx { &fnParamCtx };
            if (targetDecl.assignParam)
                assignParamCtx.declare(targetDecl.assignParam, assignArg);

            auto flow = evalCompoundStmt(assignParamCtx, at(targetDecl.body));

            for (uint32_t i = 0; i < call.args.size(); i++) {
                if (!call.args[i].isInOut)
                    continue;

                setExprValue(call.args[i], fnParamCtx.values[i]);
            }

            if (flow.kind == ControlFlowKind::None)
                return {};
            if (flow.kind == ControlFlowKind::Return)
                return flow.value;
            VERIFY_NOT_REACHED();
        });
    }
    struct CallExprRecord : ExprRecord {
        std::optional<CompleteCall> assignCall;
        CallExprRecord(Value result, std::optional<CompleteCall> assignCall)
            : ExprRecord(RecordKind::CallExpr, result), assignCall(assignCall) { }
    };
    bool cmpCompleteCallArgs(const CompleteCall& l, const CompleteCall& r) {
        if (l.args.size() != r.args.size())
            return false;
        for (uint32_t i = 0; i < l.args.size(); i++) {
            if (!cmpValue(l.args[i], r.args[i]))
                return false;
        }
        return true;
    }
    ExprResult evalCallExpr(LookupContext& ctx, CallExpr& e) {
        auto badCall = ExprResult::make<CallExprRecord>(Value {}, std::nullopt);

        Value base = evaluateExpr(ctx, e.base);
        auto baseType = typeOf(base);
        VERIFY(e.callKind == CallKind::Paren);
        auto args = evaluateArguments(ctx, e.args);

        // constructor
        if (cmpCompleteDecls(baseType, typeOverloadSetType)) {
            VERIFY(base.kind == ValueKind::Array);
            std::optional<CompleteCall> call;
            for (Value& overload : base.u.array->array()) {
                VERIFY(overload.kind == ValueKind::ParameterizedDecl);
                std::optional<CompleteCall> c = completeCall(overload.type, args);
                if (!c.has_value())
                    continue;
                if (call.has_value())
                    return badCall;
                call = std::move(c.value());
            }
            if (!call.has_value())
                return badCall;

            auto theCall = std::move(call.value());
            Value result = makeArrayValue(theCall.target, theCall.args.size());
            std::copy_n(theCall.args.data(), theCall.args.size(), result.u.array->array().data());
            return ExprResult::make<CallExprRecord>(result, theCall);
        } else if (cmpCompleteDecls(baseType, typeType)) {
            auto type = asTypeValue(base);
            auto& decl = as<StructDecl>(type.decl);
            auto posArgs = positionArguments(decl.params, {}, args);
            if (!posArgs.has_value())
                return badCall;
            CompleteCall theCall { type };
            if (!withStaticContext(type, [&](LookupContext& context) -> bool {
                    DependentScope depScope { this };
                    if (!convertFnArgs(context, decl.params, posArgs.value(), depScope.deductions))
                        return false;
                    EXPECT_EQ(depScope.deductions.size(), 0u);

                    auto valsOpt = completeFnArgs(context, decl.params, posArgs.value());
                    if (!valsOpt.has_value())
                        return false;
                    theCall.args = std::move(valsOpt.value());
                    return true;
                }))
                return badCall;

            Value result = makeArrayValue(theCall.target, theCall.args.size());
            std::copy_n(theCall.args.data(), theCall.args.size(), result.u.array->array().data());
            return ExprResult::make<CallExprRecord>(result, theCall);
        }
        // function
        else if (cmpCompleteDecls(baseType, overloadSetType)) {
            VERIFY(base.kind == ValueKind::Array);
            std::optional<CompleteCall> call;
            std::optional<CompleteCall> assignCall;
            for (Value& overload : base.u.array->array()) {
                VERIFY(overload.kind == ValueKind::ParameterizedDecl);
                std::optional<CompleteCall> c = completeCall(overload.type, args);
                if (!c.has_value())
                    continue;

                auto& decl = as<FnDecl>(c.value().target.decl);
                if (decl.assignParam) {
                    if (assignCall.has_value())
                        return badCall;
                    assignCall = std::move(c.value());
                } else {
                    if (call.has_value())
                        return badCall;
                    call = std::move(c.value());
                }
            }
            if (!call.has_value())
                return badCall;
            Value callResult = evaluateFunction(call.value());

            if (assignCall.has_value()) {
                if (!cmpCompleteCallArgs(call.value(), assignCall.value()))
                    return badCall;

                auto assignType = withStaticContext(assignCall.value().target, [&](LookupContext& context) {
                    auto& d = as<FnDecl>(assignCall.value().target.decl);
                    return toCompleteType(evaluateExpr(context, at(d.assignParam).type));
                });
                if (!assignType.valid() || !cmpCompleteDecls(assignType, typeOf(callResult)))
                    return badCall;
            }
            return ExprResult::make<CallExprRecord>(callResult, assignCall);
        }
        VERIFY_NOT_REACHED();
    }

    struct ParenExprRecord : ExprRecord {
        ExprResult base;
        ParenExprRecord(ExprResult result)
            : ExprRecord(RecordKind::ParenExpr, result.value()), base(result) { }
    };
    ExprResult evalParenExpr(LookupContext& context, ParenExpr& e) {
        return ExprResult::make<ParenExprRecord>(evaluateExpr(context, e.subExpr));
    }

    ExprResult evalUnaryOperatorExpr(LookupContext&, UnaryOperatorExpr&) { VERIFY_NOT_REACHED(); }
    ExprResult evalAccessExpr(LookupContext&, AccessExpr&) { VERIFY_NOT_REACHED(); }
    ExprResult evalImmediateBraceExpr(LookupContext&, ImmediateBraceExpr&) { VERIFY_NOT_REACHED(); }
    ExprResult evalBinaryOperatorExpr(LookupContext&, BinaryOperatorExpr&) { VERIFY_NOT_REACHED(); }

    bool setExprValue(ExprResult base, Value value) {
        switch (base.kind()) {
        case RecordKind::IdentifierExpr: {
            auto& b = base.as<IdentifierExprRecord>();
            if (!b.lValue.valid())
                return false;
            setValue(b.lValue, std::move(value));
            return true;
        }
        case RecordKind::CallExpr: {
            auto& b = base.as<CallExprRecord>();
            if (!b.assignCall.has_value())
                return false;
            evaluateFunction(b.assignCall.value(), value);
            return true;
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
            ExprResult left = evaluateExpr(context, stmt.left);
            ExprResult right = evaluateExpr(context, stmt.right);
            setExprValue(left, right.value());
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
        case ValueKind::Array:
            fmt::println("[{}] array with {} elements", sview(at(value.type.decl).name), value.u.array->size);
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

        funtion get(x&: num) {
            x = 123;
        }
        function callGet() {
            mut x = 0;
            get(x);
            return x;
        }

        mut g_globalVal: num = 0;
        function globalVal() {
            return g_globalVal;
        }
        function globalVal() = (n: num) {
            g_globalVal = n;
        }
        function updateGlobalVal() {
            get(globalVal());
            globalVal() = 456;
            return globalVal();
        }

        function wrap{T: Type}(var: T) {
            return var;
        }
        function wrap{T: Type}(var&: T) = (val: T) {
            var = val;
        }
        function updateWrappedGlobalVal() {
            get(wrap(globalVal()));
            return wrap(globalVal());
        }

        struct constant{T: Type, v: T} {
            valueMember: T = v;
        }
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
    eval("callGet()");
    eval("updateGlobalVal()");
    eval("updateWrappedGlobalVal()");
    eval("bar(mkConst{mkConst{5}()}())");
}