#include "Parser.h"
#include <algorithm>
#include <optional>
#include <vector>

struct Interpreter : STContext {

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
    struct DependentValue {
        uint32_t id = 0;
        uint32_t generation = 0;

        bool valid() { return generation != 0; }
    };
    struct CompleteDeclBase {
        Ptr<NamedDecl> decl = {};
        bool isDependentValue = false;
        bool argsDependent = false;
        uint16_t templateArgCount = 0;
        union {
            StaticLookupContext* staticContext;
            DependentValue depValue;
        } u { .staticContext = nullptr };
        ValueArray* templateAndWithArgs = nullptr;

        void clearFields() {
            *this = {};
        }
        bool valid() const { return (bool)decl || isDependentValue; }
        DependentValue asDependentValue() const {
            if (!isDependentValue)
                return {};
            return u.depValue;
        }
        bool dependentInAnyWay() const {
            return argsDependent || isDependentValue;
        }
        StaticLookupContext* staticContext() const {
            if (isDependentValue)
                return nullptr;
            return u.staticContext;
        }

        std::span<Value> templateArgs() const {
            if (!templateAndWithArgs)
                return {};
            return templateAndWithArgs->array().subspan(0, templateArgCount);
        }
        std::span<Value> withArgs() const {
            if (!templateAndWithArgs)
                return {};
            return templateAndWithArgs->array().subspan(templateArgCount);
        }
    };
    struct CompleteDecl : CompleteDeclBase {
        CompleteDecl() = default;
        CompleteDecl(Ptr<NamedDecl> decl, StaticLookupContext* staticContext = nullptr, bool argsDependent = false)
            : CompleteDeclBase { .decl = decl, .argsDependent = argsDependent, .u { .staticContext = staticContext } } { }
        CompleteDecl(DependentValue value)
            : CompleteDeclBase { .isDependentValue = true, .u { .depValue = value } } { }

        CompleteDecl(const CompleteDecl& other)
            : CompleteDeclBase(other) {
            ValueArray::ref(templateAndWithArgs);
        }
        CompleteDecl(CompleteDecl&& other)
            : CompleteDeclBase(other) { other.clearFields(); }
        CompleteDecl& operator=(const CompleteDecl& other) {
            ValueArray::deref(templateAndWithArgs);
            *(CompleteDeclBase*)this = other;
            ValueArray::ref(templateAndWithArgs);
            return *this;
        }
        CompleteDecl& operator=(CompleteDecl&& other) {
            ValueArray::deref(templateAndWithArgs);
            *(CompleteDeclBase*)this = other;
            other.clearFields();
            return *this;
        }
        ~CompleteDecl() {
            ValueArray::deref(templateAndWithArgs);
        }

        void allocateArgs(uint32_t templateArgCount, uint32_t withArgCount = 0) {
            ValueArray::deref(templateAndWithArgs);
            templateAndWithArgs = ValueArray::make(templateArgCount + withArgCount);
            this->templateArgCount = templateArgCount;
        }
    };
    struct Type : CompleteDecl {
        using CompleteDecl::CompleteDecl;
        Type(CompleteDecl decl)
            : CompleteDecl(std::move(decl)) { }
    };

    enum class ValueKind : uint8_t {
        Invalid,
        CompleteDecl,
        TemplateDecl,
        Builtin,
        Array,
        Dependent,
    };
    struct complete_t { };
    struct template_t { };
    struct Value {
        CompleteDecl type;
        union {
            ValueArray* array;
            int64_t builtinValue;
            struct {
                Ptr<NamedDecl> decl;
                bool dependent;
            } declType;
            DependentValue dependent;
        } u { .array = nullptr };
        ValueKind kind = ValueKind::Invalid;
        union {
            Word name;
            uint32_t index;
        } id { .name = {} };

        Value() = default;
        Value(Type type, int64_t value)
            : type(std::move(type)), u { .builtinValue = value }, kind(ValueKind::Builtin) { }
        Value(complete_t, Ptr<NamedDecl> type, CompleteDecl decl)
            : type(std::move(decl)), u { .declType = { type, false } }, kind(ValueKind::CompleteDecl) { }
        Value(Type type, DependentValue value)
            : type(std::move(type)), u { .dependent = value }, kind(ValueKind::Dependent) { }
        Value(Type type, uint32_t size)
            : type(std::move(type))
            , u { .array = ValueArray::make(size) }
            , kind(ValueKind::Array) { }
        Value(template_t, Ptr<NamedDecl> type, Ptr<StaticDecl> templateDecl, StaticLookupContext* staticContext)
            : type({ templateDecl, staticContext }), u { .declType = { type, false } }, kind(ValueKind::TemplateDecl) { }

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
        bool dependentInAnyWay() const {
            switch (kind) {
            case ValueKind::CompleteDecl:
                return u.declType.dependent || type.dependentInAnyWay();
            case ValueKind::Dependent:
                return true;
            default:
                return type.dependentInAnyWay();
            }
        }

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
    Value deepCopy(Value in) {
        if (in.kind != ValueKind::Array || in.u.array->refCnt == 1)
            return in;

        Value out = makeArrayValue(std::move(in.type), in.u.array->size);
        std::copy_n(in.u.array->array().data(), in.u.array->size, out.u.array->array().data());
        return out;
    }
    CompleteDecl deepCopy(CompleteDecl in) {
        ValueArray* arr = in.templateAndWithArgs;
        if (!arr || arr->refCnt == 1)
            return in;

        ValueArray* newArr = ValueArray::make(arr->size);
        std::copy_n(arr->array().data(), arr->size, newArr->array().data());
        ValueArray::deref(arr);
        in.templateAndWithArgs = newArr;
        return in;
    }
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

    enum class LookupContextKind {
        Parameter,
        Block,
        Namespace,
        Type,
        EarlyType,
        Enum,
    };
    struct LookupContext {
        LookupContext* parent = nullptr;
        LookupContextKind kind;
        LookupContext(LookupContextKind kind, LookupContext* parent)
            : parent(parent), kind(kind) { }
    };
    struct ParameterLookupContext : LookupContext {
        ParameterLookupContext(LookupContext* parent)
            : LookupContext(LookupContextKind::Parameter, parent) { }
        std::span<Ptr<Decl>> decls = {};
        std::span<Value> values = {};
    };
    struct StaticLookupContext : LookupContext {
        struct DeclValue {
            CompleteDecl decl;
            Value value;
        };
        struct DeclContext {
            CompleteDecl decl;
            StaticLookupContext* context;
        };
        CompleteDecl staticDecl;

        StaticLookupContext(LookupContextKind kind, LookupContext* parent, CompleteDecl staticDecl)
            : LookupContext(kind, parent), staticDecl(std::move(staticDecl)) { }

        std::vector<DeclValue> values;
        std::vector<DeclContext> children;
    };
    struct BlockLookupContext : LookupContext {
        std::vector<Ptr<Decl>> decls;
        std::vector<Value> values;
        BlockLookupContext(LookupContext* parent)
            : LookupContext(LookupContextKind::Block, parent) { }
        void declare(Ptr<Decl> decl, Value value) {
            EXPECT_EQ(decls.size(), values.size());
            decls.push_back(decl);
            values.emplace_back(std::move(value));
        }
    };
    struct TypeLookupContext : StaticLookupContext {
        std::span<Type> hasMemberTypes = {};
        TypeLookupContext(LookupContext* parent, CompleteDecl decl, std::span<Type> hasTypes)
            : StaticLookupContext(LookupContextKind::Type, parent, std::move(decl)), hasMemberTypes(hasTypes) { }
    };
    struct NamespaceLookupContext : StaticLookupContext {
        NamespaceLookupContext(LookupContext* parent, CompleteDecl decl)
            : StaticLookupContext(LookupContextKind::Namespace, parent, std::move(decl)) { }
    };
    struct EnumLookupContext : StaticLookupContext {
        EnumLookupContext(LookupContext* parent, CompleteDecl decl)
            : StaticLookupContext(LookupContextKind::Enum, parent, std::move(decl)) { }
    };
    struct EarlyTypeLookupContext : StaticLookupContext {
        std::vector<Word> failedLookups;
        EarlyTypeLookupContext(LookupContext* parent, CompleteDecl decl)
            : StaticLookupContext(LookupContextKind::EarlyType, parent, std::move(decl)) { }

        void checkLookups(Interpreter* i, LookupContext& fullCtx) {
            for (Word name : failedLookups)
                VERIFY(!i->performLookup(fullCtx, name).has_value());
        }
    };
    StaticLookupContext* asStaticContext(LookupContext& context) {
        if (context.kind == LookupContextKind::Type || context.kind == LookupContextKind::Namespace
            || context.kind == LookupContextKind::Enum || context.kind == LookupContextKind::EarlyType)
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

    enum class RecordKind {
        Empty,
        Basic,
        Call,
        Access,
        Identifier,
    };
    const char* toString(RecordKind kind) {
        switch (kind) {
        case RecordKind::Empty:
            return "Empty";
        case RecordKind::Basic:
            return "Basic";
        case RecordKind::Call:
            return "Call";
        case RecordKind::Identifier:
            return "Identifier";
        default:
            return "????";
        }
    }
    struct ExprRecord {
        RecordKind kind;
        uint32_t refCnt = 1;

        ExprRecord(RecordKind kind)
            : kind(kind) { }
    };
    struct BasicRecord : ExprRecord {
        BasicRecord()
            : ExprRecord(RecordKind::Basic) { }
    };
    struct ExprResult {
        ExprRecord* ptr;
        Value m_value;

        template<std::derived_from<ExprRecord> T, typename... Args>
        static ExprResult make(Value result, Args&&... args) {
            return ExprResult { std::move(result), new T(std::forward<Args>(args)...) };
        }

        void ref() {
            ptr->refCnt += 1;
        }
        void deref() {
            ptr->refCnt -= 1;
            if (ptr->refCnt == 0)
                delete ptr;
        }

        Value& value() { return m_value; }
        const Value& value() const { return m_value; }
        operator Value() const { return value(); }
        RecordKind kind() const { return ptr->kind; }
        template<typename T>
        T& as() { return *(T*)ptr; }
        Value* operator->() { return &value(); }
        const Value* operator->() const { return &value(); }

        ExprResult(const ExprResult& other)
            : ptr(other.ptr), m_value(other.value()) { ref(); }
        ExprResult& operator=(const ExprResult& other) {
            deref();
            ptr = other.ptr;
            m_value = other.value();
            ref();
            return *this;
        }
        ~ExprResult() {
            deref();
        }

    private:
        ExprResult(Value value, ExprRecord* ptr)
            : ptr(ptr), m_value(std::move(value)) { }
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
    struct FnArgumentResult : ExprResult {
        bool isInOut = false;
    };
    struct EvaluatedArguments {
        std::vector<NamedExprResult> args;
        bool dependent = false;
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

    Type typeType;
    Type fnType;
    Type intType;
    Type memberType;
    Value arrayTemplateValue;
    Value optTemplateValue;
    Type typeTemplateType;
    Type fnTemplateType;
    Type globalTemplateType;
    Value basedMemberFnTemplateValue;
    Type boolType;
    Word selfWord;
    CompleteDecl selfDecl;
    Type namespaceType;
    Value convertToTemplateValue;
    Value implicitConvertToTemplateValue;
    std::array<Value, std::to_underlying(BinaryOperator::LastOp) - std::to_underlying(BinaryOperator::FirstOp) + 1> binaryOpTraits = {};
    std::array<Value, std::to_underlying(UnaryOperator::COUNT)> unaryOpTraits = {};

    NamespaceLookupContext builtinImplContext { nullptr, {} };
    NamespaceLookupContext* globalContext = &builtinImplContext;
    using BuiltinImpl = Value (*)(Interpreter* i, std::span<const FnArgumentResult> args);
    std::span<BuiltinImpl> builtinImpls;

    ExprResult invalResult = ExprResult::make<ExprRecord>(Value {}, RecordKind::Empty);

    struct DependentValueData {
        Ptr<Expr> defaultValue = {};
        bool inconsistentDeductions = false;
        Value deduced = {};
    };
    std::vector<DependentValueData> dependentValues;
    uint32_t dependentValueGeneration = 1;

    Interpreter()
        : STContext(STContext::create()) { }

    void interpretDecls(SourceBuffer buffer) {
        Parser parser { *this, buffer };
        auto decls = parser.beginSpan<Ptr<StaticDecl>>();
        while (parser.tok.kind() != TokenKind::EOS) {
            Ptr<Decl> d;
            parser.parseDecl(d, Parser::DeclParseScope::Namespace);
            parser.append(decls, asStaticDecl(d));
        }
        globalContext = &at(make<NamespaceLookupContext>(globalContext, CompleteDecl {}));
        auto& nsDecl = makeSet<NamespaceDecl>(globalContext->staticDecl.decl, Word {});
        nsDecl.staticDecls = (Span<Ptr<StaticDecl>>)parser.finalizeSpan(decls);
    }
    Value interpretExpr(SourceBuffer buffer) {
        Parser parser { *this, buffer };
        Ptr<Expr> e;
        parser.parseBinaryExpr(e);
        return evaluateExpr(*globalContext, e);
    }

    CompleteDecl findCompDeclHelper(Word name) {
        auto r = evalIdentifierName(*globalContext, name);
        VERIFY(r.value().kind == ValueKind::CompleteDecl);
        return r.value().type;
    }
    void findBuiltins() {
        typeType = findCompDeclHelper(asWord("Type"));
        fnType = findCompDeclHelper(asWord("Function"));
        memberType = findCompDeclHelper(asWord("Member"));
        intType = findCompDeclHelper(asWord("int"));
        typeTemplateType = findCompDeclHelper(asWord("TypeTemplate"));
        fnTemplateType = findCompDeclHelper(asWord("FunctionTemplate"));
        globalTemplateType = findCompDeclHelper(asWord("VariableTemplate"));
        namespaceType = findCompDeclHelper(asWord("Namespace"));
        basedMemberFnTemplateValue = evalIdentifierName(*globalContext, asWord("BasedMemberFunction"));
        convertToTemplateValue = evalIdentifierName(*globalContext, asWord("ConvertTo"));
        implicitConvertToTemplateValue = evalIdentifierName(*globalContext, asWord("ImplicitConvertTo"));

        for (uint32_t i = 0; i < binaryOpTraits.size(); i++) {
            BinaryOperator op = (BinaryOperator)(std::to_underlying(BinaryOperator::FirstOp) + i);
            binaryOpTraits[i] = evalIdentifierName(*globalContext, asWord(toOperationString(op)));
        }
        for (uint32_t i = 0; i < unaryOpTraits.size(); i++) {
            UnaryOperator op = (UnaryOperator)i;
            unaryOpTraits[i] = evalIdentifierName(*globalContext, asWord(toOperationString(op)));
        }

        selfWord = asWord("self");
        auto selfDeclPtr = make<LocalDecl>(selfWord, false);
        selfDecl = { selfDeclPtr, nullptr };

        boolType = findCompDeclHelper(asWord("bool"));
        auto falseDecl = lookupName(globalContext, asWord("false")).value();
        VERIFY(at(falseDecl.decl).kind == DeclKind::GlobalDecl);
        ((StaticLookupContext*)falseDecl.declaringContext)->values.push_back({ CompleteDecl { falseDecl.decl }, makeBuiltinValue(boolType, 0) });
        auto trueDecl = lookupName(globalContext, asWord("true")).value();
        VERIFY(at(trueDecl.decl).kind == DeclKind::GlobalDecl);
        ((StaticLookupContext*)trueDecl.declaringContext)->values.push_back({ CompleteDecl { trueDecl.decl }, makeBuiltinValue(boolType, 1) });

        arrayTemplateValue = evalIdentifierName(*globalContext, asWord("Array"));
        optTemplateValue = evalIdentifierName(*globalContext, asWord("Opt"));

        auto implDecls = beginSpan<Ptr<StaticDecl>, 0>();
        auto impls = beginSpan<BuiltinImpl, 1>();
        auto defineImpl = [&](std::string_view name, uint32_t argCount, BuiltinImpl impl) {
            auto decl = make<FnDecl>(asWord(name));
            auto params = beginSpan<Ptr<Decl>>();
            for (uint32_t i = 0; i < argCount; i++)
                append(params, make<LocalDecl>(Word {}, false));
            at(decl).params = finalizeSpan(params);
            append(implDecls, decl);
            append(impls, impl);
        };

        defineImpl("builtinAddAndMask", 3, [](Interpreter* i, std::span<const FnArgumentResult> args) {
            return i->makeBuiltinValue(
                i->intType,
                (args[0].value().u.builtinValue + args[1].value().u.builtinValue) & args[2].value().u.builtinValue);
        });
        defineImpl("builtinMulAndMask", 3, [](Interpreter* i, std::span<const FnArgumentResult> args) {
            return i->makeBuiltinValue(
                i->intType,
                (args[0].value().u.builtinValue * args[1].value().u.builtinValue) & args[2].value().u.builtinValue);
        });
        defineImpl("builtinSignedDivAndMask", 3, [](Interpreter* i, std::span<const FnArgumentResult> args) {
            return i->makeBuiltinValue(
                i->intType,
                (args[0].value().u.builtinValue / args[1].value().u.builtinValue) & args[2].value().u.builtinValue);
        });
        defineImpl("builtinNegateAndMask", 2, [](Interpreter* i, std::span<const FnArgumentResult> args) {
            return i->makeBuiltinValue(
                i->intType,
                (-args[0].value().u.builtinValue) & args[1].value().u.builtinValue);
        });

        defineImpl("builtinCompleteTemplate", 1, [](Interpreter* i, std::span<const FnArgumentResult> args) -> Value {
            return i->completeTemplate(args[0], {});
        });

        defineImpl("builtinArraySize", 1, [](Interpreter* i, std::span<const FnArgumentResult> args) {
            return i->makeBuiltinValue(i->intType, args[0].value().u.array->size);
        });
        defineImpl("builtinArrayAppend", 2, [](Interpreter* i, std::span<const FnArgumentResult> args) {
            uint32_t oldSize = args[0].value().u.array->size;
            Value out = i->makeArrayValue(i->typeOf(args[0]), oldSize + 1);
            std::copy_n(args[0].value().u.array->array().data(), oldSize, out.u.array->array().data());
            out.u.array->array()[oldSize] = args[1];
            return out;
        });

        defineImpl("builtinOptValue", 1, [](Interpreter*, std::span<const FnArgumentResult> args) {
            VERIFY(args[0].value().kind == ValueKind::Array);
            VERIFY(args[0].value().u.array->size == 1);
            return args[0].value().u.array->array()[0];
        });

        defineImpl("builtinOptNew", 1, [](Interpreter* i, std::span<const FnArgumentResult> args) {
            EvaluatedArguments templateArgs;
            templateArgs.args.push_back({ ExprResult::make<BasicRecord>(i->makeTypeValue(i->typeOf(args[0]))), Word() });
            Type type = i->asTypeValue(i->completeTemplate(i->optTemplateValue, templateArgs));
            Value opt = i->makeArrayValue(type, 1);
            opt.u.array->array()[0] = args[0];
            return opt;
        });

        auto& nsDecl = makeSet<NamespaceDecl>(builtinImplContext.staticDecl.decl, Word {});
        nsDecl.staticDecls = finalizeSpan(implDecls);
        builtinImpls = at(finalizeSpan(impls));
    }

    Type typeOf(const Value& value) {
        if (!value.valid())
            return {};
        switch (value.kind) {
        case ValueKind::Array:
        case ValueKind::Builtin:
        case ValueKind::Dependent:
            return value.type;
        case ValueKind::CompleteDecl:
        case ValueKind::TemplateDecl:
            return { value.u.declType.decl, globalContext, value.u.declType.dependent };
        default:
            VERIFY_NOT_REACHED();
        }
    }
    CompleteDecl asCompleteDeclValue(const Value& value) {
        if (!value.valid())
            return {};
        switch (value.kind) {
        case ValueKind::CompleteDecl:
            return value.type;
        case ValueKind::Dependent: {
            return { value.u.dependent };
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }
    Type asTypeValue(const Value& value) {
        return asCompleteDeclValue(value);
    }
    std::pair<Ptr<StaticDecl>, StaticLookupContext*> asTemplateDeclValue(const Value& value) {
        VERIFY(value.kind == ValueKind::TemplateDecl);
        return { (Ptr<StaticDecl>)value.type.decl, value.type.staticContext() };
    }

    Type implicitlyToType(const Value& in) {
        if (cmpCompleteDecls(typeOf(in), typeType))
            return asTypeValue(in);
        Value converted = convertOrSlice(typeType, ExprResult::make<BasicRecord>(in));
        if (cmpCompleteDecls(typeOf(converted), typeType))
            return asTypeValue(converted);

        return {};
    }
    bool isTemplateValue(const Value& in) {
        return in.kind == ValueKind::TemplateDecl;
    }

    Value makeBuiltinValue(Type type, int64_t value) { return { std::move(type), value }; }
    Value makeDependentValue(Type type, Ptr<Expr> e = {}) {
        uint32_t id = dependentValues.size();
        dependentValues.push_back({ .defaultValue = e });
        DependentValue depValue { .id = id, .generation = dependentValueGeneration };
        return { type, depValue };
    }
    Value makeTypeValue(Type type) {
        return makeCompleteDeclValue(typeType, std::move(type));
    }
    Value makeTemplateDeclValue(Type type, Ptr<StaticDecl> templateDecl, StaticLookupContext* staticContext) {
        EXPECT_EQ(type.templateArgs().size(), 0u);
        EXPECT_EQ(type.withArgs().size(), 0u);
        VERIFY(!type.isDependentValue);
        return { template_t(), type.decl, templateDecl, staticContext };
    }
    Value makeCompleteDeclValue(const Type& type, CompleteDecl decl) {
        EXPECT_EQ(type.templateArgs().size(), 0u);
        EXPECT_EQ(type.withArgs().size(), 0u);
        if (decl.isDependentValue)
            return { type, decl.asDependentValue() };
        return { complete_t(), type.decl, std::move(decl) };
    }
    Value makeArrayValue(Type type, uint32_t size) {
        return Value { std::move(type), size };
    }

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
        case ValueKind::CompleteDecl:
            VERIFY(l.u.declType.decl == r.u.declType.decl);
            return cmpCompleteDecls(l.type, r.type);
        case ValueKind::TemplateDecl:
            VERIFY(l.u.declType.decl == r.u.declType.decl);
            VERIFY(l.type.withArgs().size() == 0);
            VERIFY(l.type.templateArgs().size() == 0);
            return l.type.decl == r.type.decl;
        default:
            VERIFY_NOT_REACHED();
        }
    }
    bool cmpCompleteDecls(const CompleteDecl& l, const CompleteDecl& r) {
        if (l.decl != r.decl)
            return false;
        auto lArgs = l.templateArgs();
        auto rArgs = r.templateArgs();
        EXPECT_EQ(lArgs.size(), rArgs.size());
        for (uint32_t i = 0; i < lArgs.size(); i++) {
            if (!cmpValue(lArgs[i], rArgs[i]))
                return false;
        }
        return true;
    }

    bool matchName(Ptr<Decl> decl, Word name) {
        if (!name)
            return true;
        auto named = asNamedDecl(decl);
        return named && at(named).name == name;
    }
    std::optional<std::vector<PositionalExprResult>> positionArguments(std::span<Ptr<Decl>> params,
        std::span<const PositionalExprResult> inArgs, std::span<const NamedExprResult> subArgs, std::optional<ExprResult> selfArg = {}) {

        std::vector<PositionalExprResult> out;
        uint32_t inOff = 0;
        uint32_t subOff = 0;
        uint32_t i = 0;
        if (params.size() > 0 && matchName(params[0], selfWord) && selfArg.has_value()) {
            if (inArgs.size() > 0 && inArgs[0].index() == 0)
                return {};
            out.push_back({ selfArg.value(), 0 });
            i += 1;
        }
        for (; i < params.size(); i++) {
            if (inOff < inArgs.size() && inArgs[inOff].index() == i) {
                out.push_back(inArgs[inOff]);
                inOff += 1;
            } else if (subOff < subArgs.size() && matchName(params[i], subArgs[subOff].name())) {
                out.push_back({ subArgs[subOff], i });
                subOff += 1;
            }
        }
        if (subOff != subArgs.size())
            return {};
        return out;
    }

    void deduce(DependentValue dep, Value val) {
        EXPECT_EQ(dependentValueGeneration, dep.generation);
        auto& out = dependentValues[dep.id];
        if (out.inconsistentDeductions)
            return;
        if (!out.deduced.valid()) {
            out.deduced = std::move(val);
        } else if (!cmpValue(out.deduced, val)) {
            out.inconsistentDeductions = true;
            out.deduced = {};
        }
    }
    bool staticMatch(const Value& source, const Value& target) {
        if (!staticMatch(typeOf(source), typeOf(target)))
            return false;

        if (target.kind == ValueKind::Dependent) {
            deduce(target.u.dependent, source);
            return true;
        }

        // TODO: When we got here the types match. Is this the right way to compare the values?
        return cmpValue(source, target);
    }
    bool staticMatch(const Type& source, const Type& target) {
        if (target.isDependentValue) {
            deduce(target.asDependentValue(), makeTypeValue(source));
            return true;
        }
        if (source.decl != target.decl)
            return false;

        auto sourceArgs = source.templateArgs();
        auto targetArgs = target.templateArgs();
        VERIFY(sourceArgs.size() == targetArgs.size());
        for (uint32_t i = 0; i < sourceArgs.size(); i++) {
            if (!staticMatch(sourceArgs[i], targetArgs[i]))
                // FIXME: previous iterations might have deduced somethings that no longer applies
                //        since we failed here.
                return false;
        }

        return true;
    }
    ExprResult convertOrSlice(Value targetTypeValue, ExprResult sourceValue, bool allowConversions = true) {
        return convertOrSlice(implicitlyToType(targetTypeValue), sourceValue, allowConversions);
    }
    struct SliceResult : ExprResult {
        enum Kind {
            Success,
            NotFound,
            Error,
        };
        Kind kind;
    };
    SliceResult slice(const Type& targetType, ExprResult sourceValue) {
        SliceResult badResult = { invalResult, SliceResult::Kind::Error };
        if (!targetType.valid())
            return badResult;

        Type sourceType = typeOf(sourceValue);

        // TODO: there is some dublication with staticMatch(Type, Type) here
        if (targetType.isDependentValue) {
            deduce(targetType.asDependentValue(), makeTypeValue(sourceType));
            return { sourceValue, SliceResult::Kind::Success };
        }
        if (targetType.decl == sourceType.decl) {
            auto sourceArgs = sourceType.templateArgs();
            auto targetArgs = targetType.templateArgs();
            VERIFY(sourceArgs.size() == targetArgs.size());
            for (uint32_t i = 0; i < sourceArgs.size(); i++) {
                if (!staticMatch(sourceArgs[i], targetArgs[i]))
                    return badResult;
            }
            return { sourceValue, SliceResult::Kind::Success };
        }

        std::vector<ExprResult> results;
        collectSlices(targetType, sourceValue, results);
        if (results.size() == 1)
            return { results[0], SliceResult::Kind::Success };
        else if (results.size() > 1)
            return badResult;

        return { invalResult, SliceResult::Kind::NotFound };
    }
    ExprResult convertOrSlice(const Type& targetType, ExprResult sourceValue, bool allowConversions = true) {
        auto sliced = slice(targetType, sourceValue);
        if (sliced.kind != SliceResult::Kind::NotFound || !allowConversions)
            return sliced;

        EvaluatedArguments convertToTemplateArgs;
        convertToTemplateArgs.args.push_back(NamedExprResult { ExprResult::make<BasicRecord>(makeTypeValue(targetType)), Word() });
        Type convertToType = asTypeValue(completeTemplate(implicitConvertToTemplateValue, convertToTemplateArgs));
        auto r = checkForHasMember(convertToType, typeOf(sourceValue));
        if (!r.has_value())
            return invalResult;
        EXPECT_EQ(at(r->decl).decls.count, 1u);
        Ptr<FnDecl> cvtDecl = (Ptr<FnDecl>)at(at(r->decl).decls, 0);
        VERIFY(at(cvtDecl).kind == DeclKind::FnDecl);
        EXPECT_EQ(at(cvtDecl).templateParams.count, 0u);
        auto* cvtDeclContext = getStaticContext(r->containingType);
        auto cvtCallBase = transformBasedMembers(slice(r->containingType, sourceValue), lookupToValue(cvtDecl, cvtDeclContext));
        return invokeCall(cvtCallBase, {}, false);
    }
    void collectSlices(const Type& type, ExprResult val, std::vector<ExprResult>& results) {
        Type valType = typeOf(val);
        if (cmpCompleteDecls(type, valType))
            return results.push_back(val);

        if (val->kind != ValueKind::Array)
            return;
        std::optional<ExprResult> out;
        auto members = at(as<StructDecl>(valType.decl).params);
        for (uint32_t i = 0; i < members.size(); i++) {
            if (at(members[i]).kind == DeclKind::HasDecl)
                collectSlices(type, accessMember(val, i), results);
        }
    }

    bool convertFnArgs(LookupContext& context, std::span<Ptr<Decl>> params, std::span<PositionalExprResult> args, bool conversionAllowed) {
        for (auto& arg : args) {
            Ptr<Expr> typeExpr = at(asVar(params[arg.index()])).type;
            if (!typeExpr)
                continue;

            Value type = evaluateExpr(context, typeExpr);
            VERIFY(type.valid());
            ExprResult cvtArg = convertOrSlice(type, arg, conversionAllowed);
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
    std::optional<std::vector<FnArgumentResult>> completeFnArgs(LookupContext& context, std::span<Ptr<Decl>> params, std::span<const PositionalExprResult> args) {
        std::vector<FnArgumentResult> out;
        uint32_t argOff = 0;
        for (uint32_t i = 0; i < params.size(); i++) {
            ExprResult arg
                = (argOff < args.size() && i == args[argOff].index())
                ? (ExprResult)args[argOff++]
                : evaluateDefaultArg(context, params[i]);

            if (!arg.value().valid())
                return {};
            out.push_back({ std::move(arg), at(asVar(params[i])).isInOut });
        }
        return out;
    }

    bool parameterizeTemplate(LookupContext& parentCtx, std::span<Ptr<LocalDecl>> allDecls, std::span<const PositionalExprResult> args, std::span<Value> output, bool& outDependent) {
        EXPECT_EQ(allDecls.size(), output.size());

        uint32_t argOff = 0;
        for (uint32_t i = 0; i < allDecls.size(); i++) {
            auto currentCtx = makeParameterContext(parentCtx, allDecls.subspan(0, i), output.subspan(0, i));
            LocalDecl& decl = at(allDecls[i]);
            Value type = {};
            if (decl.type)
                type = evaluateExpr(currentCtx, decl.type);
            else
                type = makeDependentValue(typeType);
            if (!type.valid())
                return false;

            Value& arg = output[i];
            if (argOff < args.size() && i == args[argOff].index()) {
                auto inArg = args[argOff++];
                arg = convertOrSlice(type, inArg);
                if (!arg.valid()) {
                    fmt::print("unable to initialize ");
                    dumpValue(type);
                    fmt::print("with ");
                    dumpValue(inArg);
                    return false;
                }
                for (auto& constr : at(at(allDecls[i]).valueConstraints)) {
                    Value expr = evaluateExpr(currentCtx, constr.condition);
                    if (!expr.valid())
                        continue;
                    if (!checkConstraint(arg, expr))
                        return false;
                }
            } else {
                Type theType = implicitlyToType(type);
                if (!theType.valid())
                    return false;
                arg = makeDependentValue(theType, decl.initializer);
            }
            if (arg.dependentInAnyWay())
                outDependent = true;
        }
        return true;
    }
    ExprResult completeTemplate(Value templateValue, EvaluatedArguments args) {
        auto [templateDecl, context] = asTemplateDeclValue(templateValue);
        VERIFY((bool)context);
        auto& decl = at(templateDecl);
        CompleteDecl out { templateDecl, context, args.dependent };
        out.allocateArgs(decl.templateParams.count, decl.with.params.count);

        if (!parameterizeTemplate(*context, at(decl.with.params), {}, out.withArgs(), out.argsDependent))
            return invalResult;
        auto withCtx = makeParameterContext(*context, at(decl.with.params), out.withArgs());

        auto posArgs = positionArguments(at((Span<Ptr<Decl>>)decl.templateParams), {}, args.args);
        if (!posArgs.has_value())
            return invalResult;
        if (!parameterizeTemplate(withCtx, at(decl.templateParams), posArgs.value(), out.templateArgs(), out.argsDependent))
            return invalResult;

        if (templateValue.u.declType.decl == typeTemplateType.decl)
            return ExprResult::make<BasicRecord>(makeCompleteDeclValue(typeType, out));
        if (templateValue.u.declType.decl == fnTemplateType.decl)
            return ExprResult::make<BasicRecord>(makeCompleteDeclValue(fnType, out));
        if (templateValue.u.declType.decl == typeTemplateType.decl) {
            LValue lVal = { context, out };
            return ExprResult::make<IdentifierRecord>(getValue(lVal), lVal);
        }
        VERIFY_NOT_REACHED();
    }
    bool applyDeductions(LookupContext& parentCtx, std::span<Ptr<LocalDecl>> params, std::span<Value> args) {
        EXPECT_EQ(params.size(), args.size());
        for (uint32_t i = 0; i < params.size(); i++) {
            Value& arg = args[i];
            if (arg.kind != ValueKind::Dependent)
                continue;

            auto& result = dependentValues[arg.u.dependent.id];
            if (result.inconsistentDeductions)
                return false;
            if (result.deduced.valid()) {
                arg = result.deduced;
            } else {
                auto currentCtx = makeParameterContext(parentCtx, params.subspan(0, i), args.subspan(0, i));
                arg = evaluateDefaultArg(currentCtx, params[i]);
                if (!arg.valid())
                    return false;
            }
        }
        return true;
    }
    CompleteDecl applyDeductions(CompleteDecl in) {
        in = deepCopy(std::move(in));
        auto& sDecl = as<StaticDecl>(in.decl);
        if (!applyDeductions(*in.staticContext(), at(sDecl.with.params), in.withArgs()))
            return {};
        auto withCtx = makeParameterContext(*in.staticContext(), at(sDecl.with.params), in.withArgs());
        if (!applyDeductions(withCtx, at(sDecl.templateParams), in.templateArgs()))
            return {};
        return in;
    }

    ExprResult defaultConstructType(const Type& type) {
        return invokeCall(ExprResult::make<BasicRecord>(makeTypeValue(type)), {});
    }
    ExprResult evaluateDefaultArg(LookupContext& context, Ptr<Decl> decl) {
        auto& var = at(asVar(decl));
        if (var.initializer) {
            ExprResult arg = evaluateExpr(context, var.initializer);
            if (var.type)
                arg = convertOrSlice(evaluateExpr(context, var.type), arg);
            return arg;
        }
        // FIXME: This makes sense for types but not for functions
        if (var.type) {
            return defaultConstructType(implicitlyToType(evaluateExpr(context, var.type)));
        }
        return invalResult;
    }
    struct FoundHasMember {
        Type containingType;
        Ptr<HasDecl> decl;
    };
    std::optional<FoundHasMember> checkForHasMember(const Type& shouldHave, const CompleteDecl& type) {
        TypeLookupContext* typeCtx = (TypeLookupContext*)getStaticContext(type);
        for (uint32_t i = 0; i < typeCtx->hasMemberTypes.size(); i++) {
            if (!typeCtx->hasMemberTypes[i].valid())
                continue;
            const auto& hasType = typeCtx->hasMemberTypes[i];
            if (staticMatch(shouldHave, hasType))
                return FoundHasMember { type, (Ptr<HasDecl>)at(as<StructDecl>(type.decl).params, i) };
            auto r = checkForHasMember(shouldHave, hasType);
            if (r.has_value())
                return r.value();
        }
        return {};
    }
    bool checkConstraint(const Value& value, const Value& condValue) {
        if (Type type = implicitlyToType(condValue); type.valid())
            return checkForHasMember(type, implicitlyToType(value)).has_value();

        std::array<NamedExprResult, 1> args { { { ExprResult::make<BasicRecord>(value), {} } } };
        Value res = invokeCall(ExprResult::make<BasicRecord>(condValue), args);
        VERIFY(res.valid());
        VERIFY(res.kind == ValueKind::Builtin);
        VERIFY(cmpCompleteDecls(res.type, boolType));
        return res.u.builtinValue;
    }

    EvaluatedArguments evaluateArguments(LookupContext& ctx, Arguments& a) {
        EvaluatedArguments out;
        auto args = at(a.args);
        for (auto& arg : args) {
            out.args.push_back({ evaluateExpr(ctx, arg.source), arg.target });
            out.dependent |= out.args.back().value().dependentInAnyWay();
        }
        return out;
    }

    Value& getValueRef(const LValue& lVal) {
        VERIFY(lVal.valid());
        switch (lVal.context->kind) {
        case LookupContextKind::Type:
        case LookupContextKind::EarlyType:
        case LookupContextKind::Namespace:
        case LookupContextKind::Enum: {
            auto* ctx = lVal.decl.staticContext();
            DeclKind declKind = at(lVal.decl.decl).kind;
            if (declKind == DeclKind::GlobalDecl || declKind == DeclKind::EnumValueDecl) {
                for (auto& v : ctx->values) {
                    if (cmpCompleteDecls(v.decl, lVal.decl))
                        return v.value;
                }
                Value v = initialize(lVal.decl);
                ctx->values.push_back({ lVal.decl, v });
                return ctx->values.back().value;
            }
            if (declKind == DeclKind::LocalDecl) {
                VERIFY_NOT_REACHED();
            }
            VERIFY_NOT_REACHED();
        }
        case LookupContextKind::Parameter: {
            auto* ctx = (ParameterLookupContext*)lVal.context;
            EXPECT_EQ(ctx->decls.size(), ctx->values.size());
            for (uint32_t i = 0; i < ctx->decls.size(); i++) {
                if (ctx->decls[i] == lVal.decl.decl)
                    return ctx->values[i];
            }
            VERIFY_NOT_REACHED();
        }
        case LookupContextKind::Block: {
            auto* ctx = (BlockLookupContext*)lVal.context;
            EXPECT_EQ(ctx->decls.size(), ctx->values.size());
            for (uint32_t i = 0; i < ctx->decls.size(); i++) {
                if (ctx->decls[i] == lVal.decl.decl)
                    return ctx->values[i];
            }
            VERIFY_NOT_REACHED();
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }
    Value getValue(const LValue& lVal) {
        return getValueRef(lVal);
    }
    void setValue(const LValue& lVal, Value value) {
        getValueRef(lVal) = value;
    }

    // args must not be a temporary and the values inside it may be modified
    ParameterLookupContext makeParameterContext(LookupContext& parent, std::span<Ptr<LocalDecl>> params, std::span<Value> args) {
        ParameterLookupContext context { &parent };
        context.decls = std::span<Ptr<Decl>>((Ptr<Decl>*)params.data(), params.size());
        context.values = args;
        return context;
    }
    template<typename Callback>
    auto withTemplateContext(const CompleteDecl& decl, LookupContext& ctx, Callback&& callback) {
        auto& d = at(asStaticDecl(decl.decl));
        ParameterLookupContext withCtx = makeParameterContext(ctx, at(d.with.params), decl.withArgs());
        ParameterLookupContext templateCtx = makeParameterContext(withCtx, at(d.templateParams), decl.templateArgs());
        return callback(templateCtx);
    }
    template<typename Callback>
    auto withTemplateContext(const CompleteDecl& decl, Callback&& callback) {
        return withTemplateContext(decl, *decl.staticContext(), std::forward<Callback>(callback));
    }
    Value initialize(const CompleteDecl& decl) {
        VERIFY(at(decl.decl).kind == DeclKind::GlobalDecl);
        return withTemplateContext(decl, [&](LookupContext& context) {
            auto& d = as<GlobalDecl>(decl.decl);
            ExprResult source = evaluateExpr(context, d.initializer);
            if (d.type)
                source = convertOrSlice(evaluateExpr(context, d.type), source);
            return source;
        });
    }

    ExprResult evaluateExpr(LookupContext& ctx, Ptr<Expr> p) {
        auto& e = at(p);

#define EXPR_KIND(kind)                            \
    case ExprKind::kind: {                         \
        ExprResult r = eval##kind(ctx, (kind&)e);  \
        if (!r.value().valid())                    \
            fmt::println("eval" #kind " invalid"); \
        return r;                                  \
    }

        switch (e.kind) {
            ENUMERATE_EXPR_KINDS
        default:
            VERIFY_NOT_REACHED();
        }
#undef EXPR_KIND
    }

    struct IdentifierRecord : ExprRecord {
        LValue lValue;
        IdentifierRecord(LValue lValue)
            : ExprRecord(RecordKind::Identifier), lValue(lValue) { }
    };
    struct LookupResult {
        Ptr<NamedDecl> decl = {};
        LookupContext* declaringContext = nullptr;
        LookupContext* foundInContext = nullptr;
    };
    std::optional<LookupResult> performLookup(LookupContext& context, Word name) {
        auto find = [&](std::span<Ptr<Decl>> decls) -> std::optional<LookupResult> {
            for (Ptr<Decl> decl : decls)
                if (matchName(decl, name) && isPrimaryDecl(decl))
                    return LookupResult { (Ptr<NamedDecl>)decl, &context, &context };
            return {};
        };
        switch (context.kind) {
        case LookupContextKind::Parameter:
            return find(((ParameterLookupContext&)context).decls);
        case LookupContextKind::Block:
            return find(((BlockLookupContext&)context).decls);
        case LookupContextKind::Enum:
        case LookupContextKind::Namespace:
            return find(at((Span<Ptr<Decl>>)as<StaticDecl>(((NamespaceLookupContext&)context).staticDecl.decl).staticDecls));
        case LookupContextKind::Type:
        case LookupContextKind::EarlyType: {
            auto& structDecl = as<StructDecl>(((StaticLookupContext&)context).staticDecl.decl);
            VERIFY(structDecl.kind == DeclKind::StructDecl);
            auto memberResult = find(at(structDecl.params));
            auto staticResult = find(at((Span<Ptr<Decl>>)structDecl.staticDecls));
            VERIFY(!(memberResult.has_value() && staticResult.has_value()));
            if (staticResult.has_value())
                return staticResult;
            if (memberResult.has_value())
                return memberResult;

            if (context.kind == LookupContextKind::EarlyType) {
                ((EarlyTypeLookupContext&)context).failedLookups.push_back(name);
                return {};
            }

            auto& ctx = (TypeLookupContext&)context;
            std::optional<LookupResult> hasResult = {};
            EXPECT_EQ(ctx.hasMemberTypes.size(), structDecl.params.count);
            for (uint32_t i = 0; i < structDecl.params.count; i++) {
                auto memberDecl = at(structDecl.params, i);
                if (at(memberDecl).kind != DeclKind::HasDecl)
                    continue;
                auto& hasDecl = as<HasDecl>(memberDecl);
                auto r = find(at((Span<Ptr<Decl>>)hasDecl.decls));
                if (!r.has_value())
                    r = performLookup(*getStaticContext(ctx.hasMemberTypes[i]), name);
                if (r.has_value()) {
                    VERIFY(!hasResult.has_value());
                    hasResult = r;
                }
            }
            if (hasResult.has_value()) {
                hasResult.value().foundInContext = &context;
                return hasResult;
            }
            return {};
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }
    std::optional<LookupResult> lookupName(LookupContext* context, Word name, LookupContext* end = nullptr) {
        while (context != end) {
            auto r = performLookup(*context, name);
            if (r.has_value())
                return r;
            context = context->parent;
        }
        if (name != selfWord)
            fmt::println("looking up {} failed", sview(name));
        return {};
    }
    ExprResult lookupToValue(LookupResult result) {
        return lookupToValue(result.decl, result.declaringContext);
    }
    ExprResult lookupToValue(Ptr<NamedDecl> decl, LookupContext* declContext) {
        DeclKind declKind = at(decl).kind;
        switch (declKind) {
        case DeclKind::LocalDecl: {
            if (declContext->kind == LookupContextKind::Block || declContext->kind == LookupContextKind::Parameter) {
                LValue lVal { declContext, CompleteDecl { decl } };
                return ExprResult::make<IdentifierRecord>(getValue(lVal), lVal);
            }
            if (declContext->kind == LookupContextKind::Type || declContext->kind == LookupContextKind::EarlyType)
                return ExprResult::make<BasicRecord>(makeCompleteDeclValue(memberType, CompleteDecl { decl, (StaticLookupContext*)declContext }));
            VERIFY_NOT_REACHED();
        }
        case DeclKind::EnumValueDecl:
            return ExprResult::make<BasicRecord>(getValue(LValue { declContext, CompleteDecl { decl, (StaticLookupContext*)declContext } }));
        case DeclKind::EnumDecl:
        case DeclKind::NamespaceDecl: {
            const auto& type = declKind == DeclKind::EnumDecl ? typeType : namespaceType;
            auto* staticContext = asStaticContext(*declContext);
            VERIFY((bool)staticContext);
            return ExprResult::make<BasicRecord>(makeCompleteDeclValue(type, CompleteDecl { decl, staticContext }));
        }
        case DeclKind::StructDecl:
        case DeclKind::FnDecl:
        case DeclKind::GlobalDecl: {
            StaticDecl& sDecl = as<StaticDecl>(decl);
            auto* staticContext = asStaticContext(*declContext);
            VERIFY((bool)staticContext);
            if (sDecl.templateParams.count == 0) {
                VERIFY(sDecl.with.params.count == 0);
                if (declKind == DeclKind::GlobalDecl) {
                    LValue lVal { declContext, CompleteDecl { decl, staticContext } };
                    return ExprResult::make<IdentifierRecord>(getValue(lVal), lVal);
                }
                const Type& type = declKind == DeclKind::StructDecl ? typeType : fnType;
                return ExprResult::make<BasicRecord>(makeCompleteDeclValue(type, CompleteDecl { decl, staticContext }));
            }
            Type type = {};
            if (declKind == DeclKind::StructDecl)
                type = typeTemplateType;
            else if (declKind == DeclKind::FnDecl)
                type = fnTemplateType;
            else if (declKind == DeclKind::GlobalDecl)
                type = globalTemplateType;

            return ExprResult::make<BasicRecord>(makeTemplateDeclValue(type, (Ptr<StaticDecl>)decl, staticContext));
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }
    ExprResult evalIdentifier(LookupContext& ctx, Identifier id) {
        auto lookup = lookupName(&ctx, id.word);
        if (!lookup.has_value())
            return invalResult;
        ExprResult result = lookupToValue(lookup.value());
        if (isTemplateValue(result) && id.hasBraces)
            result = completeTemplate(result, evaluateArguments(ctx, id));

        auto selfLookup = lookupName(&ctx, selfWord);
        // TODO: ->parent->parent->parent is kind of ugly
        if (selfLookup.has_value() && selfLookup->foundInContext->parent->parent->parent == lookup->foundInContext) {
            ExprResult base = lookupToValue(selfLookup.value());
            result = transformBasedMembers(base, result);
        }
        return result;
    }
    ExprResult evalIdentifierName(LookupContext& ctx, Word name) {
        auto r = lookupName(&ctx, name);
        if (!r.has_value())
            return invalResult;
        return lookupToValue(r.value());
    }
    ExprResult evalIdentifierExpr(LookupContext& ctx, IdentifierExpr& e) {
        return evalIdentifier(ctx, e.identifier);
    }

    ExprResult evalIntLiteralExpr(LookupContext&, IntLiteralExpr& e) {
        return ExprResult::make<BasicRecord>(makeBuiltinValue(intType, e.value));
    }
    ExprResult evalCompoundExpr(LookupContext& parent, CompoundExpr& e) {
        BlockLookupContext context { &parent };
        auto stmts = at(e.body);
        for (uint32_t i = 0; i < stmts.size() - 1; i++) {
            auto flow = evaluateStmt(context, stmts[i]);
            VERIFY(flow.kind == ControlFlowKind::None);
        }
        auto& lastStmt = at(stmts.back());
        VERIFY(lastStmt.kind == StmtKind::ExprStmt);
        Value res = evaluateExpr(context, ((ExprStmt&)lastStmt).expr);
        return ExprResult::make<BasicRecord>(res);
    }

    struct CompleteCall {
        CompleteDecl target;
        std::vector<FnArgumentResult> args = {};
    };
    std::optional<CompleteCall> completeCall(CompleteDecl fnDecl, std::span<const NamedExprResult> namedFnArgs, std::optional<ExprResult> selfArg, bool conversionsAllowed) {
        auto& fn = as<CallableDecl>(fnDecl.decl);
        auto posFnArgs = positionArguments(at(fn.params), {}, namedFnArgs, selfArg);
        if (!posFnArgs.has_value())
            return {};

        EarlyTypeLookupContext earlyTypeCtx { nullptr, fnDecl };

        if (!withTemplateContext(fnDecl, [&](LookupContext& context) {
                // Early type context since we don't know all the template arguments yet.
                // This is fine since the type expressions are evaluated in this context, which
                // have to be constant expressions.
                earlyTypeCtx.parent = &context;
                LookupContext* contextToUse = nullptr;
                if (fn.kind == DeclKind::StructDecl)
                    contextToUse = &earlyTypeCtx;
                else if (fn.kind == DeclKind::FnDecl)
                    contextToUse = &context;
                else
                    VERIFY_NOT_REACHED();

                if (!convertFnArgs(*contextToUse, at(fn.params), posFnArgs.value(), conversionsAllowed))
                    return false;
                // TODO: slice self arg
                /*if (posFnArgs.value().size() > 0 && posFnArgs.value()[0].index() == 0
                    && fn.kind == DeclKind::FnDecl && matchName(at(fn.params, 0), selfWord)) {
                    VERIFY(fnDecl.staticContext()->staticDecl.valid());
                    posFnArgs.value()[0] = { slice(fnDecl.staticContext()->staticDecl, posFnArgs.value()[0]), 0 };
                }*/

                return true;
            }))
            return {};

        CompleteCall out;
        out.target = applyDeductions(std::move(fnDecl));
        if (!out.target.valid())
            return {};

        // Now the template arguments are deduced so we can make the proper context.
        // This crutial since default member initializers can access mutable static members.
        auto callback = [&](LookupContext& ctx) { return completeFnArgs(ctx, at(fn.params), posFnArgs.value()); };
        std::optional<std::vector<FnArgumentResult>> fnArgs;
        if (fn.kind == DeclKind::FnDecl) {
            fnArgs = withTemplateContext(out.target, callback);
        } else {
            StaticLookupContext* ctx = getStaticContext(out.target);
            earlyTypeCtx.checkLookups(this, *ctx);
            fnArgs = callback(*ctx);
        }

        if (!fnArgs.has_value())
            return {};
        out.args = std::move(fnArgs.value());

        return out;
    }
    Value evaluateCall(CompleteCall call) { return evaluateCall(std::move(call), {}); }
    Value evaluateCall(CompleteCall call, Value assignArg) {
        auto& decl = at(call.target.decl);
        if (decl.kind == DeclKind::FnDecl)
            return evaluateFunction(std::move(call), assignArg);
        if (decl.kind == DeclKind::StructDecl) {
            if (assignArg.valid()) {
                VERIFY(assignArg.kind == ValueKind::Array);
                VERIFY(cmpCompleteDecls(assignArg.type, call.target));
                for (uint32_t i = 0; i < call.args.size(); i++)
                    VERIFY(setExprValue(call.args[i], assignArg.u.array->array()[i]));
                return {};
            } else {
                Value result = makeArrayValue(call.target, call.args.size());
                std::copy_n(call.args.data(), call.args.size(), result.u.array->array().data());
                return result;
            }
        }
        VERIFY_NOT_REACHED();
    }
    Value evaluateFunction(CompleteCall call, Value assignArg) {
        VERIFY(at(call.target.decl).kind == DeclKind::FnDecl);
        if (call.target.staticContext() == &builtinImplContext) {
            for (uint32_t i = 0; i < builtinImpls.size(); i++) {
                if (call.target.decl == at(as<NamespaceDecl>(builtinImplContext.staticDecl.decl).staticDecls, i))
                    return builtinImpls[i](this, call.args);
            }
        }

        auto& targetDecl = as<FnDecl>(call.target.decl);

        return withTemplateContext(call.target, *call.target.staticContext(), [&](LookupContext& templateCtx) -> Value {
            ParameterLookupContext fnParamCtx { &templateCtx };
            std::vector<Value> fnArgs;
            fnParamCtx.decls = at(targetDecl.params);
            for (uint32_t i = 0; i < call.args.size(); i++)
                fnArgs.push_back(call.args[i]);
            fnParamCtx.values = fnArgs;

            BlockLookupContext assignParamCtx { &fnParamCtx };
            if (targetDecl.assignParam)
                assignParamCtx.declare(targetDecl.assignParam, assignArg);

            Value retValue = {};
            if (targetDecl.body) {
                auto flow = evaluateStmt(assignParamCtx, targetDecl.body);
                if (flow.kind == ControlFlowKind::Return)
                    retValue = flow.value;
            } else if (targetDecl.bodyExpr) {
                retValue = evaluateExpr(assignParamCtx, targetDecl.bodyExpr);
            } else
                VERIFY_NOT_REACHED();

            for (uint32_t i = 0; i < call.args.size(); i++) {
                if (!call.args[i].isInOut)
                    continue;
                VERIFY(setExprValue(call.args[i], fnParamCtx.values[i]));
            }

            return retValue;
        });
    }
    struct CallRecord : ExprRecord {
        CompleteCall call;
        CallRecord(CompleteCall call)
            : ExprRecord(RecordKind::Call), call(call) { }
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
    ExprResult invokeCall(ExprResult baseR, std::span<const NamedExprResult> args, bool conversionsAllowed = true) {
        Value base = baseR;
        auto baseType = typeOf(base);

        // constructor
        if (Type type = implicitlyToType(base); type.valid()) {
            auto call = completeCall(type, args, {}, conversionsAllowed);
            if (!call.has_value())
                return invalResult;
            return ExprResult::make<CallRecord>(evaluateCall(call.value()), call.value());
        }
        // function
        std::optional<ExprResult> selfArg;
        auto [basedMemberFnTemplateDecl, _] = asTemplateDeclValue(basedMemberFnTemplateValue);
        if (baseType.decl == basedMemberFnTemplateDecl) {
            VERIFY(base.kind == ValueKind::Array);
            selfArg = accessMember(baseR, 0);
            base = baseType.templateArgs()[1];
            baseType = typeOf(base);
        }
        if (cmpCompleteDecls(baseType, fnTemplateType)) {
            base = completeTemplate(base, {});
            baseType = typeOf(base);
        }
        if (cmpCompleteDecls(baseType, fnType)) {
            VERIFY(base.kind == ValueKind::CompleteDecl);
            auto baseDecl = asCompleteDeclValue(base);
            auto call = completeCall(baseDecl, args, selfArg, conversionsAllowed);
            if (!call.has_value())
                return invalResult;

            return ExprResult::make<CallRecord>(evaluateCall(call.value()), call.value());
        }
        fmt::print("cannot call ");
        dumpValue(base);
        fmt::print("of type ");
        dumpValue(makeTypeValue(baseType));
        return invalResult;
    }
    ExprResult evalCallExpr(LookupContext& ctx, CallExpr& e) {
        VERIFY(e.callKind == CallKind::Paren);
        ExprResult base = evaluateExpr(ctx, e.base);
        auto args = evaluateArguments(ctx, e.args);
        return invokeCall(base, args.args);
    }

    ExprResult evalParenExpr(LookupContext& context, ParenExpr& e) {
        if (e.args.args.count == 1)
            return ExprResult::make<BasicRecord>(evaluateExpr(context, at(e.args.args, 0).source));
        VERIFY_NOT_REACHED();
    }

    struct AccessRecord : ExprRecord {
        ExprResult base;
        Ptr<Decl> accessDecl;
        AccessRecord(ExprResult base, Ptr<Decl> accessDecl)
            : ExprRecord(RecordKind::Access), base(base), accessDecl(accessDecl) { }
    };
    StaticLookupContext* getStaticContext(const CompleteDecl& decl) {
        if (!decl.dependentInAnyWay()) {
            for (auto& child : decl.staticContext()->children) {
                if (cmpCompleteDecls(child.decl, decl))
                    return child.context;
            }
        }

        auto& d = as<StaticDecl>(decl.decl);
        auto& withCtx = at(make<ParameterLookupContext>(makeParameterContext(*decl.staticContext(), at(d.with.params), decl.withArgs())));
        auto& templateCtx = at(make<ParameterLookupContext>(makeParameterContext(withCtx, at(d.templateParams), decl.templateArgs())));

        StaticLookupContext* ctx = nullptr;
        if (d.kind == DeclKind::NamespaceDecl) {
            ctx = &at(make<NamespaceLookupContext>(&templateCtx, decl));
        } else if (d.kind == DeclKind::EnumDecl) {
            ctx = &at(make<EnumLookupContext>(&templateCtx, decl));
            int64_t currentValue = 0;
            for (auto declInDecl : at(((EnumDecl&)d).staticDecls)) {
                if (at(declInDecl).kind == DeclKind::EnumValueDecl) {
                    EnumValueDecl& d = as<EnumValueDecl>(declInDecl);
                    EXPECT_EQ(d.templateParams.count, 0u);
                    EXPECT_EQ(d.with.params.count, 0u);
                    CompleteDecl compDecl { (Ptr<NamedDecl>)declInDecl, ctx };
                    if (d.enumValue) {
                        Value value = evaluateExpr(*ctx, d.enumValue);
                        VERIFY(value.valid());
                        VERIFY(cmpCompleteDecls(typeOf(value), decl));
                        VERIFY(value.kind == ValueKind::Builtin);
                        currentValue = value.u.builtinValue;
                    }
                    ctx->values.push_back({ compDecl, makeBuiltinValue(decl, currentValue++) });
                }
            }
        } else if (d.kind == DeclKind::StructDecl) {
            if (decl.dependentInAnyWay()) {
                // TODO: check that the context made here can never escape the current stmt
                ctx = &at(make<EarlyTypeLookupContext>(&templateCtx, decl));
            } else {
                EarlyTypeLookupContext earlyCtx { &templateCtx, decl };
                auto& structDecl = as<StructDecl>(decl.decl);
                auto memberCount = structDecl.params.count;
                Type* hasTypes = &at(allocate<Type>(memberCount));
                std::uninitialized_fill_n(hasTypes, memberCount, Type {});
                for (uint32_t i = 0; i < memberCount; i++) {
                    Ptr<Decl> member = at(structDecl.params, i);
                    if (at(member).kind != DeclKind::HasDecl)
                        continue;
                    auto& hasDecl = as<HasDecl>(member);
                    Type type = implicitlyToType(evaluateExpr(earlyCtx, hasDecl.type));
                    VERIFY(type.valid());
                    hasTypes[i] = std::move(type);
                }

                ctx = &at(make<TypeLookupContext>(&templateCtx, decl, std::span<Type>(hasTypes, memberCount)));
                earlyCtx.checkLookups(this, *ctx);
            }
        } else
            VERIFY_NOT_REACHED();
        if (!decl.dependentInAnyWay())
            decl.staticContext()->children.push_back({ decl, ctx });
        return ctx;
    }
    ExprResult accessMember(ExprResult base, uint32_t i) {
        auto members = at(as<StructDecl>(base.value().type.decl).params);
        return ExprResult::make<AccessRecord>(base.value().u.array->array()[i], base, members[i]);
    }
    ExprResult evalAccessExpr(LookupContext& context, AccessExpr& e) {
        ExprResult base = evaluateExpr(context, e.base);
        CompleteDecl staticDecl = {};
        if (e.isStatic) {
            if (cmpCompleteDecls(typeOf(base.value()), namespaceType)) {
                VERIFY(base.value().kind == ValueKind::CompleteDecl);
                staticDecl = base.value().type;
            } else
                staticDecl = implicitlyToType(base.value());
        } else
            staticDecl = typeOf(base.value());

        std::optional<LookupResult> lookup = performLookup(*getStaticContext(staticDecl), e.member.word);
        if (!lookup.has_value()) {
            fmt::print("failed to access {} on ", sview(e.member.word));
            dumpValue(base);
            return invalResult;
        }
        ExprResult result = lookupToValue(lookup.value());
        if (isTemplateValue(result) && e.member.hasBraces)
            result = completeTemplate(result, evaluateArguments(context, e.member));
        return transformBasedMembers(base, result);
    }

    ExprResult transformBasedMembers(ExprResult base, ExprResult result) {
        Type resultType = typeOf(result);
        if (cmpCompleteDecls(resultType, fnType) || cmpCompleteDecls(resultType, fnTemplateType)) {
            EvaluatedArguments templateArgs;
            templateArgs.args.emplace_back(ExprResult::make<BasicRecord>(makeTypeValue(typeOf(base))), Word {});
            templateArgs.args.emplace_back(result, Word {});
            ExprResult type = completeTemplate(basedMemberFnTemplateValue, templateArgs);
            if (!type.value().valid())
                return invalResult;
            NamedExprResult baseArg { base, Word {} };
            return invokeCall(type, { &baseArg, 1 });
        }
        if (cmpCompleteDecls(resultType, memberType)) {
            auto memberDecl = asCompleteDeclValue(result);
            auto* typeCtx = memberDecl.staticContext();
            VERIFY(typeCtx->kind == LookupContextKind::Type || typeCtx->kind == LookupContextKind::EarlyType);
            base = slice(typeCtx->staticDecl, base);
            VERIFY(base->kind == ValueKind::Array);
            auto members = at(as<StructDecl>(typeCtx->staticDecl.decl).params);
            EXPECT_EQ(members.size(), base->u.array->size);
            for (uint32_t i = 0; i < members.size(); i++) {
                if (members[i] == memberDecl.decl)
                    return ExprResult::make<AccessRecord>(base->u.array->array()[i], base, memberDecl.decl);
            }
            VERIFY_NOT_REACHED();
        }
        return result;
    }

    ExprResult invokeOp(Value traitValue, ExprResult base, std::span<NamedExprResult> args) {
        if (isTemplateValue(traitValue)) {
            EvaluatedArguments templateArgs;
            for (NamedExprResult& arg : args)
                templateArgs.args.push_back(NamedExprResult { ExprResult::make<BasicRecord>(makeTypeValue(typeOf(arg))), Word() });
            traitValue = completeTemplate(traitValue, templateArgs);
        }
        Type targetType = asTypeValue(traitValue);

        VERIFY(base->valid());
        auto r = checkForHasMember(targetType, typeOf(base));
        if (!r.has_value())
            return invalResult;
        EXPECT_EQ(at(r->decl).decls.count, 1u);
        Ptr<FnDecl> opDecl = (Ptr<FnDecl>)at(at(r->decl).decls, 0);
        VERIFY(at(opDecl).kind == DeclKind::FnDecl);
        EXPECT_EQ(at(opDecl).templateParams.count, 0u);
        auto* containingCtx = getStaticContext(r->containingType);
        auto opCallBase = transformBasedMembers(slice(r->containingType, base), lookupToValue(opDecl, containingCtx));
        return invokeCall(opCallBase, args);
    }
    ExprResult evalUnaryOperatorExpr(LookupContext& context, UnaryOperatorExpr& e) {
        ExprResult base = evaluateExpr(context, e.subExpr);
        return invokeOp(unaryOpTraits[std::to_underlying(e.op)], base, {});
    }
    ExprResult evalBinaryOperatorExpr(LookupContext& context, BinaryOperatorExpr& e) {
        ExprResult left = evaluateExpr(context, e.left);
        ExprResult right = evaluateExpr(context, e.right);
        if (!isCmpOp(e.op)) {
            NamedExprResult rightNamed = NamedExprResult { right, Word() };
            return invokeOp(binaryOpTraits[std::to_underlying(e.op) - std::to_underlying(BinaryOperator::FirstOp)], left, std::span<NamedExprResult> { &rightNamed, 1 });
        }
        auto checkOp = [](std::partial_ordering order, BinaryOperator op) -> bool {
            switch (op) {
            case BinaryOperator::Less:
                return order < 0;
            case BinaryOperator::LessEqual:
                return order <= 0;
            case BinaryOperator::Equal:
                return order == 0;
            case BinaryOperator::GreaterEqual:
                return order >= 0;
            case BinaryOperator::Greater:
                return order > 0;
            default:
                VERIFY_NOT_REACHED();
            }
        };

        Type leftType = typeOf(left);
        Type rightType = typeOf(right);
        if ((at(leftType.decl).kind == DeclKind::EnumDecl && cmpCompleteDecls(leftType, rightType))
            || (cmpCompleteDecls(leftType, intType) && cmpCompleteDecls(rightType, intType))) {
            VERIFY(left->kind == ValueKind::Builtin);
            VERIFY(right->kind == ValueKind::Builtin);
            return ExprResult::make<BasicRecord>(makeBuiltinValue(boolType, checkOp(left->u.builtinValue <=> right->u.builtinValue, e.op)));
        }
        VERIFY_NOT_REACHED();
    }
    ExprResult evalConstraintExpr(LookupContext&, ConstraintExpr&) {
        Value typeValue = makeDependentValue(typeType);
        return ExprResult::make<BasicRecord>(makeDependentValue(asTypeValue(typeValue)));
    }

    bool checkArguments(LookupContext& parentCtx, std::span<Ptr<LocalDecl>> params, std::span<Value> args) {
        EXPECT_EQ(params.size(), args.size());
        for (uint32_t i = 0; i < params.size(); i++) {
            auto currentCtx = makeParameterContext(parentCtx, params.subspan(0, i), args.subspan(0, i));
            Ptr<Expr> typeExpr = at(params[i]).type;
            if (!typeExpr)
                return false;
            auto type = implicitlyToType(evaluateExpr(currentCtx, typeExpr));
            if (!type.valid() || !cmpCompleteDecls(type, typeOf(args[i])))
                return false;
        }
        return true;
    }
    bool transformFnArguments(LookupContext& context, std::span<Ptr<Decl>> params, std::span<FnArgumentResult> args) {
        EXPECT_EQ(params.size(), args.size());
        for (uint32_t i = 0; i < params.size(); i++) {
            args[i].isInOut = as<LocalDecl>(params[i]).isInOut;
            Ptr<Expr> typeExpr = as<LocalDecl>(params[i]).type;
            if (!typeExpr)
                continue;
            auto type = implicitlyToType(evaluateExpr(context, typeExpr));
            VERIFY(type.valid());
            VERIFY(cmpCompleteDecls(type, typeOf(args[i])));
        }
        return true;
    }
    bool setExprValue(ExprResult base, Value value) {
        bool debug = false;
        switch (base.kind()) {
        case RecordKind::Identifier: {
            auto& b = base.as<IdentifierRecord>();
            if (!b.lValue.valid())
                return false;
            if (debug) {
                fmt::print("identifier {} = ", declName(b.lValue.decl.decl));
                dumpValue(value);
            }
            setValue(b.lValue, std::move(value));
            return true;
        }
        case RecordKind::Call: {
            auto& b = base.as<CallRecord>();
            DeclKind kind = at(b.call.target.decl).kind;
            CompleteDecl target = b.call.target;
            if (kind == DeclKind::FnDecl) {
                VERIFY((bool)target.staticContext());
                FnDecl& targetDecl = as<FnDecl>(target.decl);
                Ptr<FnDecl> assignDeclPtr = {};
                for (Ptr<Decl> decl : at(as<StaticDecl>(target.staticContext()->staticDecl.decl).staticDecls)) {
                    if (isNamedDecl(decl) && at(decl).kind == DeclKind::FnDecl
                        && as<NamedDecl>(decl).name == targetDecl.name
                        && decl != target.decl) {
                        VERIFY(!assignDeclPtr);
                        assignDeclPtr = (Ptr<FnDecl>)decl;
                    }
                }
                VERIFY((bool)assignDeclPtr);
                auto& assignDecl = at(assignDeclPtr);
                VERIFY(checkArguments(*target.staticContext(), at(assignDecl.with.params), target.withArgs()));
                auto withCtx = makeParameterContext(*target.staticContext(), at(assignDecl.with.params), target.withArgs());
                VERIFY(checkArguments(withCtx, at(assignDecl.templateParams), target.templateArgs()));
                auto templateCtx = makeParameterContext(withCtx, at(assignDecl.templateParams), target.templateArgs());
                CompleteCall assignCall = b.call;
                assignCall.target.decl = assignDeclPtr;
                VERIFY(transformFnArguments(templateCtx, at(assignDecl.params), assignCall.args));
                Type assignArgType = implicitlyToType(evaluateExpr(templateCtx, at(assignDecl.assignParam).type));
                VERIFY(assignArgType.valid());
                VERIFY(cmpCompleteDecls(typeOf(value), assignArgType));
                if (debug) {
                    fmt::print("assign call {} = ", declName(assignCall.target.decl));
                    dumpValue(value);
                }
                evaluateCall(assignCall, value);
                return true;
            }
            if (kind == DeclKind::StructDecl) {
                VERIFY(value.kind == ValueKind::Array);
                VERIFY(cmpCompleteDecls(value.type, b.call.target));
                if (debug) {
                    fmt::print("constructor {} = ", declName(b.call.target.decl));
                    dumpValue(value);
                }
                for (uint32_t i = 0; i < b.call.args.size(); i++) {
                    if (!setExprValue(b.call.args[i], value.u.array->array()[i]))
                        return false;
                }
                return true;
            }
            VERIFY_NOT_REACHED();
        }
        case RecordKind::Access: {
            auto& b = base.as<AccessRecord>();
            if (!b.accessDecl)
                return false;
            Value baseValue = b.base;
            VERIFY(baseValue.kind == ValueKind::Array);
            auto members = at(as<StructDecl>(baseValue.type.decl).params);
            for (uint32_t i = 0; i < members.size(); i++) {
                if (members[i] == b.accessDecl) {
                    Value newVal = deepCopy(baseValue);
                    newVal.u.array->array()[i] = value;
                    if (debug) {
                        fmt::print("member {}::{} = ", declName(baseValue.type.decl), declName(members[i]));
                        dumpValue(value);
                    }
                    return setExprValue(b.base, newVal);
                }
            }
            VERIFY_NOT_REACHED();
        }
        default:
            fmt::print("attempt to assign ");
            dumpValue(value);
            fmt::print("to basic expr resulting in ");
            dumpValue(base);
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
        ExprResult init = evaluateExpr(context, info.initializer);
        if (info.type)
            init = convertOrSlice(evaluateExpr(context, info.type), init);

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

    bool evaluateExprToBool(LookupContext& context, Ptr<Expr> e) {
        Value v = evaluateExpr(context, e);
        // TODO: apply conversions
        VERIFY(cmpCompleteDecls(typeOf(v), boolType));
        VERIFY(v.kind == ValueKind::Builtin);
        return (bool)v.u.builtinValue;
    }

    ControlFlow evalIfStmt(LookupContext& context, IfStmt& stmt) {
        if (evaluateExprToBool(context, stmt.condition)) {
            BlockLookupContext localCtx { &context };
            PROPEGATE_FLOW(evaluateStmt(localCtx, stmt.ifTrue));
        } else if (stmt.ifFalse) {
            BlockLookupContext localCtx { &context };
            PROPEGATE_FLOW(evaluateStmt(localCtx, stmt.ifFalse));
        }
        return {};
    }

    ControlFlow evalForStmt(LookupContext& parent, ForStmt& stmt) {
        ExprResult range = evaluateExpr(parent, stmt.rangeExpr);
        VERIFY(range->kind == ValueKind::Array);
        ParameterLookupContext iteratorCtx { &parent };
        iteratorCtx.decls = { &(Ptr<Decl>&)stmt.loopVarDecl, 1 };
        for (Value& value : range->u.array->array()) {
            iteratorCtx.values = { &value, 1 };
            BlockLookupContext localCtx { &iteratorCtx };
            PROPEGATE_FLOW(evaluateStmt(localCtx, stmt.body));
        }
        return {};
    }
    ControlFlow evalWhileStmt(LookupContext& parent, WhileStmt& stmt) {
        while (evaluateExprToBool(parent, stmt.condition)) {
            BlockLookupContext localCtx { &parent };
            PROPEGATE_FLOW(evaluateStmt(localCtx, stmt.body));
        }
        return {};
    }

    ControlFlow evalNullStmt(LookupContext&, NullStmt&) { return {}; }

    ControlFlow evalAssignStmt(LookupContext& context, AssignStmt& stmt) {
        if (!stmt.op.has_value()) {
            ExprResult left = evaluateExpr(context, stmt.left);
            ExprResult right = evaluateExpr(context, stmt.right);
            VERIFY(setExprValue(left, right.value()));
        } else
            VERIFY_NOT_REACHED();
        return {};
    }

    std::string_view declName(Ptr<Decl> d) {
        if (!d)
            return "Invalid Decl";
        if (!isNamedDecl(d))
            return "";
        return sview(as<NamedDecl>(d).name);
    }
    void dumpValue(const Value& value) {
        switch (value.kind) {
        case ValueKind::Invalid:
            fmt::println("Invalid Value");
            break;
        case ValueKind::Builtin:
            fmt::println("[{}] {}", declName(value.type.decl), value.u.builtinValue);
            break;
        case ValueKind::CompleteDecl:
            fmt::println("[{}] {}{}", declName(value.u.declType.decl), value.u.declType.dependent ? "dependent " : "", declName(value.type.decl));
            if (auto sDecl = asStaticDecl(value.type.decl)) {
                for (uint32_t i = 0; i < value.type.templateArgs().size(); i++) {
                    auto memberDecl = at(at(sDecl).templateParams, i);
                    if (at(memberDecl).kind == DeclKind::LocalDecl)
                        fmt::print("  '{}': ", declName(memberDecl));
                    else
                        VERIFY_NOT_REACHED();
                    dumpValue(value.type.templateArgs()[i]);
                }
            }
            break;
        case ValueKind::TemplateDecl:
            fmt::println("[{}] {}", declName(value.u.declType.decl), declName(value.type.decl));
            break;
        case ValueKind::Dependent:
            fmt::println("[{}] dependend #{}", declName(value.type.decl), value.u.dependent.id);
            break;
        case ValueKind::Array:
            fmt::println("[{}] array with {} elements", declName(value.type.decl), value.u.array->size);
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
        struct Type: {}
        struct Function: {}
        struct Member: {}
        struct Template: {}
        struct TypeTemplate: {
            has Template;
            has ImplicitConvertTo{Type}: {
                fn convert(self) => builtinCompleteTemplate(self);
            }
        }
        struct FunctionTemplate: {
            has Template;
        }
        struct VariableTemplate: {
            has Template;
        }
        struct Namespace: {}

        template(T: Type)
        struct Array: {
            fn append(self&, element: T): {
                self = builtinArrayAppend(self, element);
            }
            fn size(self) => builtinArraySize(self);
        }
        
        template(T: Type)
        struct Opt: {
            fn new(value: T) => builtinOptNew(value);
            fn value(self) => builtinOptValue(self);
            fn value(self&) = (value: T): {
                self = new(value);
            }
        }

        template(T: Type, F: Function)
        struct BasedMemberFunction: {
            base: T;
        }

        template(T: Type)
        struct ConvertTo: {}
        template(T: Type)
        struct ImplicitConvertTo: { has ConvertTo{T}; }

        template(T: Type) struct Add: {}
        template(T: Type) struct Sub: {}
        template(T: Type) struct BitAnd: {}
        template(T: Type) struct LogAnd: {}
        template(T: Type) struct BitXor: {}
        template(T: Type) struct BitOr: {}
        template(T: Type) struct LogOr: {}
        template(T: Type) struct Mul: {}
        template(T: Type) struct Div: {}
        template(T: Type) struct Rem: {}
        template(T: Type) struct Shl: {}
        template(T: Type) struct Shr: {}

        struct BitNot: {}
        struct PreInc: {}
        struct PreDec: {}
        struct LogNot: {}
        struct Plus: {}
        struct Neg: {}
        struct PostInc: {}
        struct PostDec: {}

        struct bool: {
            has LogAnd{bool}: {
                fn logAnd(self, b: bool): {
                    if self: {
                        if b: return true;
                    }
                    return false;
                }
            }
            has LogOr{bool}: {
                fn logOr(self, b: bool): {
                    if !self: {
                        if !b: return false;
                    }
                    return true;
                }
            }
            has LogNot: {
                fn logNot(self): {
                    if self: return false;
                    return true;
                }
            }
        }
        true: bool = ();
        false: bool = ();

        struct int: {
            static INT_MASK: int = 0xffff'ffff'ffff'ffff;

            has Add{int}: {
                fn add(self, r: int) => builtinAddAndMask(self, r, INT_MASK);
            }
            has Sub{int}: {
                fn sub(self, r: int) => add(r.neg());
            }
            has Neg: {
                fn neg(self) => builtinNegateAndMask(self, INT_MASK);
            }
            has Mul{int}: {
                fn mul(self, r: int) => builtinMulAndMask(self, r, INT_MASK);
            }
            has Div{int}: {
                fn div(self, r: int) => builtinSignedDivAndMask(self, r, INT_MASK);
            }
        }

        template(T: Type)
        fn typeOf(arg: T) => T;
    )str");
    it.findBuiltins();
    it.interpretDecls(R"str(
        fn foo(x: bool): {
            if x:
                x = foo(false);
            return x;
        }

        fn get(x&: int): {
            x = 123;
        }
        fn callGet(): {
            mut x = 0;
            get(x);
            return x;
        }

        mut g_globalVal: int = 0;
        fn globalVal() => g_globalVal;
        fn globalVal() = (n: int): {
            g_globalVal = n;
        }
        fn updateGlobalVal(): {
            get(globalVal());
            globalVal() = 456;
            return globalVal();
        }

        template(T: Type)
        fn wrap(var: T) => var;
        template(T: Type)
        fn wrap(var&: T) = (val: T): {
            var = val;
        }
        fn updateWrappedGlobalVal(): {
            get(wrap(globalVal()));
            return wrap(globalVal());
        }

        template(T: Type, v: T)
        struct constant: {
            value: T = v;
        }
        with(T: Type) template(v: T)
        fn mkConst() => constant{T, v}();

        template(a: int, A: Type)
        fn bar(b: constant{A, a}) => a;
        template(a: int, A: Type, b: constant{A, a}, B: Type)
        fn bar(c: constant{B, b}) => a;
        template(a: int, A: Type, b: constant{A, a}, B: Type, c: constant{B, b}, C: Type)
        fn bar(d: constant{C, c}) => a;

        struct A: {
            x: int = 0;
            y: int = 0;
        }
        fn testA(): {
            let a = A(789);
            let x: int = 1;
            let y: int = 2;
            A(x, y) = a;
            return A(x, y).x;
        }

        namespace baseNS: {

        struct Base: {
            x: int = 0;
            fn set(self&, y: int): { x = y; }
            fn get(self) => x;

            fn test(self&): {
                self.set(7);
                return self.get();
            }
            fn test2(self&): {
                set(8);
                return get();
            }
        }
        fn testBase() => [
            mut b = Base();
            b.test();
            b.test2()
        ];

        struct HasBase: {
            has Base: {
                fn test(self): {
                    return 2;
                }
            }
            fn callGet(self) => get();
        }

        template(T ?Base: Type)
        fn hasBase() => true;
        template(b: ?Base)
        fn hasBase2() => b;

        }

        struct Flags: {
            flag1: bool;
            flag2: bool;
            flag3: bool;
            flag4: bool;
        }
        fn allTrue(f: Flags) => f.flag1 && f.flag2 && f.flag3 && f.flag4;
        fn atLeastOneFalse(f: Flags) => !allTrue(f);
        template(f ?allTrue: Flags)
        fn conditionFlags() => true;
        template(f ?atLeastOneFalse: Flags)
        fn conditionFlags() => false;

        enum MyEnum: {
            A; B; C; D = B; E;
        }

        struct MyInt: {
            value: int = 0;

            has ImplicitConvertTo{int}: {
                fn convert(self) => value;
            }
        }

        fn testArray(): {
            let arr = Array{int}();
            arr.append(0);
            arr.append(1);
            arr.append(arr.size());
            return arr;
        }

        fn testFor(): {
            let arr = testArray();
            for i&: int in arr:
                i = i + 1;
            return arr;
        }

        fn factorial(n: int): {
            mut r: int = 1;
            while (n > 0): {
                r = r * n;
                n = n - 1;
            }
            return r;
        }

        fn assignOpt(i: int): {
            mut opt = Opt::new(0);
            opt.value() = i;
            return opt;
        }
    )str");

    auto eval = [&](const char* expr) {
        Interpreter::Value v = it.interpretExpr(expr);
        fmt::print("eval: ");
        it.dumpValue(v);
    };
    eval("foo(true)"); // -> false
    eval("callGet()"); // -> 123
    eval("updateGlobalVal()"); // -> 456
    eval("updateWrappedGlobalVal()"); // -> 456
    // eval("bar(mkConst{mkConst{5}()}())");
    eval("testA()"); // -> 789
    eval("baseNS::testBase()"); // -> 8
    eval("baseNS::HasBase().get()"); // -> 0
    eval("baseNS::HasBase(baseNS::Base(1)).x"); // -> 1
    eval("baseNS::HasBase().test()"); // -> 2
    eval("(5 * 4 - 2) / 3"); // -> 6
    eval("!(true && false)"); // -> true
    // eval("conditionFlags{Flags(true, true, true, false)}()");
    // eval("conditionFlags{Flags(true, true, true, true)}()");
    // eval("baseNS::hasBase{baseNS::HasBase}()");
    // eval("baseNS::hasBase2{A()}()");
    // eval("baseNS::hasBase2{baseNS::HasBase()}()");
    eval("MyEnum::B"); // -> 1
    eval("MyEnum::E"); // -> 2
    eval("wrap{int}(MyInt(3))"); // -> 3
    eval("wrap{constant}(mkConst{4}()).value"); // -> 4
    eval("testArray()"); // -> (0, 1, 2)
    eval("testFor()"); // -> (1, 2, 3)
    eval("factorial(4)"); // -> 24
    eval("typeOf(Opt::new(4))"); // -> Opt{int}
    eval("assignOpt(4)"); // -> 4
}