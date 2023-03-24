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
    struct DependentValue {
        Ptr<LocalDecl> decl;
        uint32_t level = 0;
    };
    struct CompleteDeclBase {
        Ptr<NamedDecl> decl = {};
        bool argsDependent = false;
        bool declDependent = false;
        uint16_t argCount = 0;
        union {
            StaticLookupContext* staticContext;
            uint32_t declDepLevel;
        } u { .staticContext = nullptr };
        ValueArray* argsAndWithArgs = nullptr;

        StaticLookupContext* staticContext() const {
            if (declDependent)
                return nullptr;
            return u.staticContext;
        }
        void clearFields() {
            *this = {};
        }
        bool valid() const { return (bool)decl; }
        bool dependentInAnyWay() const { return declDependent || argsDependent; }
        DependentValue asDependentValue() const {
            if (!declDependent)
                return {};
            return { (Ptr<LocalDecl>)decl, u.declDepLevel };
        }

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
        CompleteDecl(Ptr<NamedDecl> decl, StaticLookupContext* staticContext = nullptr, bool argsDependent = false)
            : CompleteDeclBase { .decl = decl, .argsDependent = argsDependent, .u { .staticContext = staticContext } } { }
        CompleteDecl(DependentValue value)
            : CompleteDeclBase { .decl = value.decl, .argsDependent = false, .declDependent = true, .u { .declDepLevel = value.level } } { }

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
            struct {
                Ptr<NamedDecl> decl;
                bool dependent;
            } declType;
            DependentValue dependent;
        } u { .array = nullptr };
        ValueKind kind = ValueKind::Invalid;
        bool constraint = false;
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
        Value(parameterized_t, Ptr<NamedDecl> type, ParameterizedDecl decl)
            : type(std::move(decl)), u { .declType = { type, false } }, kind(ValueKind::ParameterizedDecl) { }

        Value(const Value& other)
            : type(other.type), u(other.u), kind(other.kind), constraint(other.constraint), id(other.id) {
            ref();
        }
        Value& operator=(const Value& other) {
            deref();
            type = other.type;
            u = other.u;
            kind = other.kind;
            constraint = other.constraint;
            id = other.id;
            ref();
            return *this;
        }
        Value(Value&& other)
            : type(std::move(other.type)), u(other.u), kind(other.kind), constraint(other.constraint), id(other.id) {
            other.kind = ValueKind::Invalid;
        }
        Value& operator=(Value&& other) {
            deref();
            type = std::move(other.type);
            u = other.u;
            kind = other.kind;
            constraint = other.constraint;
            id = other.id;
            other.kind = ValueKind::Invalid;
            return *this;
        }

        bool valid() const { return kind != ValueKind::Invalid; }
        bool dependentInAnyWay() const {
            switch (kind) {
            case ValueKind::CompleteDecl:
            case ValueKind::ParameterizedDecl:
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
        StaticRedirect,
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
    struct TypeLookupContext;
    struct StaticLookupContext : LookupContext {
        struct DeclValue {
            CompleteDecl decl;
            Value value;
        };
        struct DeclContext {
            CompleteDecl decl;
            TypeLookupContext* context;
        };
        CompleteDecl staticDecl;

        StaticLookupContext(LookupContext* parent, CompleteDecl staticDecl)
            : LookupContext(LookupContextKind::Static, parent), staticDecl(std::move(staticDecl)) { }

        std::vector<DeclValue> values;
        std::vector<DeclContext> children;
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
    struct StaticRedirectLookupContext : LookupContext {
        StaticLookupContext* redirect;
        StaticRedirectLookupContext(LookupContext* parent, StaticLookupContext* redirect)
            : LookupContext(LookupContextKind::StaticRedirect, parent), redirect(redirect) { }
    };
    struct TypeLookupContext : StaticLookupContext {
        LookupContext* parametricContext;
        TypeLookupContext(LookupContext* parent, CompleteDecl decl)
            : StaticLookupContext(parent, std::move(decl)), parametricContext(parent) { }
    };
    StaticLookupContext* asStaticContext(LookupContext& context) {
        if (context.kind == LookupContextKind::Static)
            return static_cast<StaticLookupContext*>(&context);
        if (context.kind == LookupContextKind::StaticRedirect)
            return static_cast<StaticRedirectLookupContext&>(context).redirect;
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
    Type intType;
    ParameterizedDecl arrayDecl;
    Type overloadType;
    Type overloadSetType;
    ParameterizedDecl memberOverloadSetDecl;
    Type typeOverloadType;
    Type typeOverloadSetType;
    Type boolType;
    Word selfWord;
    CompleteDecl selfDecl;

    StaticLookupContext builtinImplContext { nullptr, {} };
    StaticLookupContext* globalContext = &builtinImplContext;
    using BuiltinImpl = Value (*)(Interpreter* i, std::span<const FnArgumentResult> args);
    std::span<BuiltinImpl> builtinImpls;

    ExprResult invalResult = ExprResult::make<ExprRecord>(Value {}, RecordKind::Empty);

    struct Deduction {
        Ptr<LocalDecl> decl;
        Value value;
    };
    struct DependentScope {
        Interpreter* i;
        uint32_t level;
        DependentScope(Interpreter* i)
            : i(i), level(i->dependenceLevel() + 1) {
            i->dependentStack.emplace_back();
        }
        ~DependentScope() {
            EXPECT_EQ(i->dependenceLevel(), level);
            i->dependentStack.pop_back();
        }
    };
    std::vector<std::vector<Deduction>> dependentStack;
    uint32_t dependenceLevel() { return dependentStack.size() - 1; }

    Interpreter()
        : STContext(STContext::create()) { }

    void interpretDecls(SourceBuffer buffer) {
        Parser parser { *this, buffer };
        parser.dumpTokens = true;
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
        intType = findCompDeclHelper(asWord("int"));
        typeType = findCompDeclHelper(asWord("Type"));
        overloadType = findCompDeclHelper(asWord("Overload"));
        overloadSetType = findCompDeclHelper(asWord("OverloadSet"));
        typeOverloadType = findCompDeclHelper(asWord("TypeOverload"));
        typeOverloadSetType = findCompDeclHelper(asWord("TypeOverloadSet"));

        selfWord = asWord("self");
        auto selfDeclPtr = make<LocalDecl>(selfWord, false);
        selfDecl = { selfDeclPtr, nullptr };

        boolType = findCompDeclHelper(asWord("bool"));
        auto falseDecl = findCompDeclHelper(asWord("false"));
        VERIFY(at(falseDecl.decl).kind == DeclKind::GlobalDecl);
        falseDecl.staticContext()->values.push_back({ falseDecl, makeBuiltinValue(boolType, 0) });
        auto trueDecl = findCompDeclHelper(asWord("true"));
        VERIFY(at(trueDecl.decl).kind == DeclKind::GlobalDecl);
        trueDecl.staticContext()->values.push_back({ trueDecl, makeBuiltinValue(boolType, 1) });

        arrayDecl = findDeclHelper(asWord("Array"));
        VERIFY(at(arrayDecl.decl).kind == DeclKind::StructDecl);
        memberOverloadSetDecl = findDeclHelper(asWord("MemberOverloadSet"));
        VERIFY(at(memberOverloadSetDecl.decl).kind == DeclKind::StructDecl);

        auto implDecls = beginSpan<Ptr<Decl>, 0>();
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

        builtinImplContext.decls = at(finalizeSpan(implDecls));
        builtinImpls = at(finalizeSpan(impls));
    }

    Type typeOf(const Value& value) {
        if (value.constraint || !value.valid())
            return {};
        switch (value.kind) {
        case ValueKind::Array:
        case ValueKind::Builtin:
        case ValueKind::Dependent:
            return value.type;
        case ValueKind::CompleteDecl:
        case ValueKind::ParameterizedDecl:
            return { value.u.declType.decl, globalContext, value.u.declType.dependent };
        default:
            VERIFY_NOT_REACHED();
        }
    }
    Type asTypeValue(const Value& value) {
        if (value.constraint || !value.valid())
            return {};
        switch (value.kind) {
        case ValueKind::CompleteDecl:
            VERIFY(value.u.declType.decl == typeType.decl);
            return value.type;
        case ValueKind::Dependent: {
            VERIFY(cmpCompleteDecls(value.type, typeType));
            return { value.u.dependent };
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    bool implicitlyConvertibleToType(const Type& in) {
        return cmpCompleteDecls(in, typeType) || cmpCompleteDecls(in, typeOverloadSetType);
    }
    Value implicitlyToTypeValue(const Value& in) {
        if (in.constraint)
            return in;
        return makeTypeValue(implicitlyToType(in));
    }
    Type implicitlyToType(const Value& in) {
        Type inType = typeOf(in);
        if (cmpCompleteDecls(inType, typeType))
            return asTypeValue(in);

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
    Value asConstraintValue(Value in) {
        in.constraint = false;
        return in;
    }

    Value makeBuiltinValue(Type type, int64_t value) { return { std::move(type), value }; }
    Value makeDependentValue(Type type, Ptr<LocalDecl> depDecl) {
        return { std::move(type), DependentValue { depDecl, dependenceLevel() } };
    }
    Value makeTypeValue(Type type) {
        if (type.declDependent)
            return { typeType, type.asDependentValue() };
        return { complete_t(), typeType.decl, std::move(type) };
    }
    Value makeArrayValue(Type type, uint32_t size) {
        return Value { std::move(type), size };
    }
    Value makeParameterizedDeclValue(Ptr<NamedDecl> type, ParameterizedDecl decl) {
        return Value { parameterized_t(), type, std::move(decl) };
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
        case ValueKind::ParameterizedDecl:
            return cmpParameterizedDecls(l.type, r.type);
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
    bool cmpParameterizedDecls(const ParameterizedDecl& l, const ParameterizedDecl& r) {
        if (l.decl != r.decl)
            return false;
        if (l.args().size() != r.args().size())
            return false;
        for (uint32_t i = 0; i < l.args().size(); i++) {
            if (l.args()[i].index() != r.args()[i].index())
                return false;
            if (!cmpValue(l.args()[i], r.args()[i]))
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
        dependentStack[dep.level].push_back({ dep.decl, std::move(val) });
    }
    bool isUndeduce(Ptr<Decl> decl, const Value& v) {
        return v.kind == ValueKind::Dependent && v.u.dependent.decl == decl && v.u.dependent.level == dependenceLevel();
    }
    bool staticMatch(const Value& source, const Value& target) {
        if (target.constraint)
            return checkConstraint(source, asConstraintValue(target));

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
        if (target.declDependent) {
            deduce(target.asDependentValue(), makeTypeValue(source));
            return true;
        }
        if (source.decl != target.decl)
            return false;

        VERIFY(source.args().size() == target.args().size());
        for (uint32_t i = 0; i < source.args().size(); i++) {
            if (!staticMatch(source.args()[i], target.args()[i]))
                // FIXME: previous iterations might have deduced somethings that no longer applies
                //        since we failed here.
                return false;
        }

        return true;
    }
    ExprResult convertOrSlice(Value targetTypeValue, ExprResult sourceValue, bool allowConversions = true) {
        if (targetTypeValue.constraint)
            return sourceValue;
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
        if (targetType.declDependent) {
            deduce(targetType.asDependentValue(), makeTypeValue(sourceType));
            return { sourceValue, SliceResult::Kind::Success };
        }
        if (targetType.decl == sourceType.decl) {
            for (uint32_t i = 0; i < sourceType.args().size(); i++) {
                if (!staticMatch(sourceType.args()[i], targetType.args()[i]))
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

        Type sourceType = typeOf(sourceValue);
        // TODO: should this be a conversion?
        if (implicitlyConvertibleToType(sourceType) && cmpCompleteDecls(targetType, typeType)) {
            return ExprResult::make<BasicRecord>(makeTypeValue(implicitlyToType(sourceValue)));
        }

        EvaluatedArguments parametricArgs;
        parametricArgs.args.push_back({ ExprResult::make<BasicRecord>(makeTypeValue(targetType)), {} });
        ExprResult convFn = lookupToValue(lookupIdentifier(*globalContext, conversionWord(), parametricArgs));
        std::vector<NamedExprResult> fnArgs { { sourceValue, {} } };
        return invokeCall(convFn, fnArgs);
    }
    void collectSlices(const Type& type, ExprResult val, std::vector<ExprResult>& results) {
        Type valType = typeOf(val);
        if (cmpCompleteDecls(type, valType))
            return results.push_back(val);

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

    std::optional<ParameterLookupContext> makeParameterContext(LookupContext& parent, std::span<Ptr<LocalDecl>> allDecls, std::span<const PositionalValue> args) {
        ParameterLookupContext context { &parent };
        uint32_t argOff = 0;

        for (uint32_t i = 0; i < allDecls.size(); i++) {
            Value arg;
            Value type = evaluateExpr(context, at(allDecls[i]).type);
            if (argOff < args.size() && i == args[argOff].index()) {
                auto inArg = args[argOff++];
                if (!type.valid())
                    continue;
                arg = convertOrSlice(type, ExprResult::make<BasicRecord>(inArg));
                if (!arg.valid()) {
                    fmt::print("unable to initialize ");
                    dumpValue(type);
                    fmt::print("with ");
                    dumpValue(inArg);
                    return {};
                }
                for (auto& constr : at(at(allDecls[i]).valueConstraints)) {
                    Value expr = evaluateExpr(context, constr.condition);
                    if (!expr.valid())
                        continue;
                    if (!checkConstraint(arg, expr))
                        return {};
                }
            } else {
                auto completeType = implicitlyToType(type);
                if (!completeType.valid())
                    return {};
                arg = makeDependentValue(completeType, (Ptr<LocalDecl>)allDecls[i]);
            }
            context.decls = std::span<Ptr<Decl>>((Ptr<Decl>*)allDecls.data(), i + 1);
            context.appendValue(arg);
        }
        EXPECT_EQ(argOff, args.size());

        return context;
    }
    void applyDeductions(LocalLookupContext& context) {
        auto& deductions = dependentStack.back();
        for (uint32_t i = 0; i < context.decls.size(); i++) {
            auto decl = context.decls[i];
            auto& value = context.values[i];
            for (auto& deduc : deductions) {
                if (decl != (Ptr<Decl>)deduc.decl)
                    continue;

                if (deduc.value.dependentInAnyWay())
                    continue;
                if (isUndeduce(decl, value)) {
                    // value was deduced
                    value = deduc.value;
                } else if (value.dependentInAnyWay()) {
                    VERIFY_NOT_REACHED();
                } else if (!cmpValue(value, deduc.value)) {
                    // value was deduced multiple times but not to the same value
                    value = {};
                }
            }
        }
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
        if (var.type) {
            return defaultConstructType(implicitlyToType(evaluateExpr(context, var.type)));
        }
        return invalResult;
    }
    bool recursivelyCheckForHasMember(const Type& shouldHave, const CompleteDecl& type) {
        if (staticMatch(type, shouldHave))
            return true;

        auto* typeCtx = getTypeContext(type);
        for (LookupContext* ctx = typeCtx->parent; ctx != typeCtx->parametricContext; ctx = ctx->parent) {
            VERIFY(ctx->kind == LookupContextKind::StaticRedirect);
            const auto& decl = ((StaticRedirectLookupContext*)ctx)->redirect->staticDecl;
            if (recursivelyCheckForHasMember(shouldHave, decl))
                return true;
        }
        return false;
    }
    bool checkConstraint(const Value& value, const Value& condValue) {
        Type condType = typeOf(condValue);
        if (implicitlyConvertibleToType(condType))
            return recursivelyCheckForHasMember(implicitlyToType(condValue), implicitlyToType(value));

        if (cmpCompleteDecls(condType, overloadSetType)) {
            std::array<NamedExprResult, 1> args { { { ExprResult::make<BasicRecord>(value), {} } } };
            Value res = invokeCall(ExprResult::make<BasicRecord>(condValue), args);
            VERIFY(res.valid());
            VERIFY(res.kind == ValueKind::Builtin);
            VERIFY(cmpCompleteDecls(res.type, boolType));
            return res.u.builtinValue;
        }

        VERIFY_NOT_REACHED();
    }
    bool completeParameterContext(std::span<Value> out, LocalLookupContext& context, std::span<const PositionalValue> args, bool& outDependent) {
        uint32_t argOff = 0;
        for (uint32_t i = 0; i < context.decls.size(); i++) {
            Ptr<LocalDecl> decl = (Ptr<LocalDecl>)context.decls[i];
            auto& value = context.values[i];
            if (!value.valid())
                return false;

            Value& arg = out[i];
            if (isUndeduce(decl, value)) {
                if (argOff < args.size() && args[argOff].index() == i) {
                    arg = args[argOff];
                } else if (at(decl).initializer) {
                    // no argument was provided and nothing was deduced
                    // -> use the default arugment
                    arg = evaluateExpr(context, at(decl).initializer);
                }
                if (at(decl).type)
                    arg = convertOrSlice(evaluateExpr(context, at(decl).type), ExprResult::make<BasicRecord>(arg));
            } else {
                // argument should already be converted
                arg = value;
                if (at(decl).type && !staticMatch(makeTypeValue(typeOf(arg)), implicitlyToTypeValue(evaluateExpr(context, at(decl).type))))
                    return false;
            }
            if (!arg.valid())
                return false;
            if (arg.dependentInAnyWay()) {
                outDependent = true;
                continue;
            }
            for (auto constr : at(at(decl).valueConstraints)) {
                if (!checkConstraint(arg, evaluateExpr(context, constr.condition)))
                    return false;
            }
        }
        return true;
    }
    template<typename Callback>
    CompleteDecl withDependentParametricContext(const ParameterizedDecl& decl, Callback&& callback) {
        DependentScope depScope { this };

        Ptr<StaticDecl> sDecl = asStaticDecl(decl.decl);
        VERIFY((bool)sDecl);
        VERIFY((bool)decl.staticContext());

        auto withCtx = makeParameterContext(*decl.staticContext(), at(at(sDecl).with.params), {});
        if (!withCtx.has_value())
            return {};

        auto paramCtx = makeParameterContext(withCtx.value(), at(at(sDecl).parametric), decl.args());
        if (!paramCtx.has_value())
            return {};

        if (!callback(paramCtx.value()))
            return {};

        applyDeductions(withCtx.value());
        applyDeductions(paramCtx.value());

        CompleteDecl out { sDecl, decl.staticContext() };
        out.allocateArgs(paramCtx.value().decls.size(), withCtx.value().decls.size());

        if (!completeParameterContext(out.withArgs(), withCtx.value(), {}, out.argsDependent))
            return {};

        if (!completeParameterContext(out.args(), paramCtx.value(), decl.args(), out.argsDependent))
            return {};

        return out;
    }
    CompleteDecl completeDecl(const ParameterizedDecl& decl) {
        if (!isStaticDecl(decl.decl)) {
            VERIFY(decl.staticContext() == nullptr);
            VERIFY(decl.args().size() == 0);
            return CompleteDecl { decl.decl };
        }
        return withDependentParametricContext(decl, [](LookupContext&) { return true; });
    }

    struct EvaluatedArguments {
        std::vector<NamedExprResult> args;
        bool dependent = false;
    };
    EvaluatedArguments evaluateArguments(LookupContext& ctx, Arguments& a) {
        EvaluatedArguments out;
        auto args = at(a.args);
        for (auto& arg : args) {
            out.args.push_back({ evaluateExpr(ctx, arg.source), arg.target });
            out.dependent |= out.args.back().value().dependentInAnyWay();
        }
        return out;
    }

    LookupResult lookupIdentifierIn(LookupContext& context, Word name, const EvaluatedArguments& args) {
        LookupResult out;
        for (Ptr<Decl> decl : context.decls) {
            if (!isNamedDecl(decl))
                continue;
            if (as<NamedDecl>(decl).name != name)
                continue;

            out.context = &context;
            ParameterizedDecl parameterized { (Ptr<NamedDecl>)decl, asStaticContext(context), args.dependent };
            if (parameterized.staticContext()) {
                Ptr<StaticDecl> sDecl = asStaticDecl(decl);
                VERIFY((bool)sDecl);
                auto posArgs = positionArguments(at((Span<Ptr<Decl>>)at(sDecl).parametric), {}, args.args);
                if (!posArgs.has_value())
                    continue;

                parameterized.allocateArgs(args.args.size());
                for (uint32_t i = 0; i < args.args.size(); i++)
                    parameterized.args()[i] = posArgs.value()[i];
            } else
                VERIFY(args.args.empty());

            if (out.declKind != DeclKind::Invalid && out.declKind != at(decl).kind) {
                out.decls.clear();
                break;
            }
            out.declKind = at(decl).kind;
            out.decls.push_back(std::move(parameterized));
        }
        return out;
    }
    LookupResult lookupIdentifier(LookupContext& identCtx, Word name, const EvaluatedArguments& args, LookupContext* end = nullptr) {
        // fmt::println("looking up '{}'", sview(name));
        LookupContext* context = &identCtx;
        while (context != end) {
            LookupResult r = lookupIdentifierIn(*context, name, args);
            if (r.valid())
                return r;

            context = context->parent;
        }
        if (name != selfWord)
            fmt::println("looking up '{}' failed", sview(name));
        return {};
    }
    LookupResult lookupIdentifier(LookupContext& identCtx, Identifier ident, LookupContext* end = nullptr) {
        return lookupIdentifier(identCtx, ident.word, evaluateArguments(identCtx, ident), end);
    }
    Value& getValueRef(const LValue& lVal) {
        VERIFY(lVal.valid());
        VERIFY((bool)asVar(lVal.decl.decl));
        switch (lVal.context->kind) {
        case LookupContextKind::Static:
            VERIFY(lVal.context == lVal.decl.staticContext());
            [[fallthrough]];
        case LookupContextKind::StaticRedirect:
            return getStaticValueRef(lVal.decl);
        case LookupContextKind::Local: {
            LocalLookupContext* lContext = (LocalLookupContext*)lVal.context;
            EXPECT_EQ(lContext->decls.size(), lContext->values.size());
            for (uint32_t i = 0; i < lContext->decls.size(); i++) {
                if (lContext->decls[i] == lVal.decl.decl)
                    return lContext->values[i];
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
    Value& getStaticValueRef(const CompleteDecl& decl) {
        VERIFY((bool)decl.staticContext());
        auto context = decl.staticContext();
        for (auto& v : context->values) {
            if (cmpCompleteDecls(v.decl, decl))
                return v.value;
        }
        Value v = initialize(decl);
        context->values.push_back({ decl, v });
        return context->values.back().value;
    }
    Value getStaticValue(const CompleteDecl& decl) {
        return getStaticValueRef(decl);
    }
    void setStaticValue(const CompleteDecl& decl, Value v) {
        getStaticValueRef(decl) = std::move(v);
    }

    // args must not be a temporary and the values inside it may be modified
    LocalLookupContext makeCompleteParameterContext(LookupContext& parent, std::span<Ptr<LocalDecl>> params, std::span<Value> args) {
        LocalLookupContext context { &parent };
        context.decls = std::span<Ptr<Decl>>((Ptr<Decl>*)params.data(), params.size());
        context.values = args;
        return context;
    }
    template<typename Callback>
    auto withParametricContext(const CompleteDecl& decl, LookupContext& ctx, Callback&& callback) {
        auto& d = at(asStaticDecl(decl.decl));
        LocalLookupContext withCtx = makeCompleteParameterContext(ctx, at(d.with.params), decl.withArgs());
        LocalLookupContext paramCtx = makeCompleteParameterContext(withCtx, at(d.parametric), decl.args());
        return callback(paramCtx);
    }
    template<typename Callback>
    auto withParametricContext(const CompleteDecl& decl, Callback&& callback) {
        return withParametricContext(decl, *decl.staticContext(), std::forward<Callback>(callback));
    }
    Value initialize(const CompleteDecl& decl) {
        VERIFY(at(decl.decl).kind == DeclKind::GlobalDecl);
        return withParametricContext(decl, [&](LookupContext& context) {
            auto& d = as<GlobalDecl>(decl.decl);
            ExprResult source = evaluateExpr(context, d.initializer);
            if (d.type)
                source = convertOrSlice(evaluateExpr(context, d.type), source);
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

    struct IdentifierRecord : ExprRecord {
        LValue lValue;
        IdentifierRecord(LValue lValue)
            : ExprRecord(RecordKind::Identifier), lValue(lValue) { }
    };
    ExprResult lookupToValue(LookupResult r) {
        if (!r.valid())
            return invalResult;
        switch (r.declKind) {
        case DeclKind::GlobalDecl:
        case DeclKind::LocalDecl: {
            LValue lVal = toLValue(r);
            if (lVal.valid())
                return ExprResult::make<IdentifierRecord>(getValue(lVal), lVal);
            return invalResult;
        }
        case DeclKind::FnDecl:
        case DeclKind::StructDecl: {
            const auto& setType = r.declKind == DeclKind::StructDecl ? typeOverloadSetType : overloadSetType;
            const auto& itemType = r.declKind == DeclKind::StructDecl ? typeOverloadType : overloadType;
            Value v = makeArrayValue(setType, r.decls.size());
            for (uint32_t i = 0; i < r.decls.size(); i++)
                v.u.array->array()[i] = makeParameterizedDeclValue(itemType.decl, std::move(r.decls[i]));
            return ExprResult::make<BasicRecord>(v);
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }
    bool isTypeSubContext(TypeLookupContext* typeCtx, LookupContext* foundCtx) {
        LookupContext* testCtx = typeCtx;
        do {
            if (testCtx == foundCtx)
                return true;
            testCtx = testCtx->parent;
        } while (testCtx != typeCtx->parametricContext);
        return false;
    }
    ExprResult evalIdentifierExpr(LookupContext& ctx, IdentifierExpr& e) {
        LookupResult result = lookupIdentifier(ctx, e.identifier);
        ExprResult value = lookupToValue(result);
        auto selfResult = lookupIdentifier(ctx, { {}, selfWord });

        if (selfResult.valid()
            && result.declKind == DeclKind::FnDecl
            && isTypeSubContext((TypeLookupContext*)selfResult.context->parent, result.context)) {
            return transformMemberOverloadSet(lookupToValue(selfResult), value);
        }
        return value;
    }

    ExprResult evalIntLiteralExpr(LookupContext&, IntLiteralExpr& e) {
        return ExprResult::make<BasicRecord>(makeBuiltinValue(intType, e.value));
    }

    struct CompleteCall {
        CompleteDecl target;
        std::vector<FnArgumentResult> args = {};
    };
    std::optional<CompleteCall> completeCall(
        const ParameterizedDecl& fnDecl, std::span<const NamedExprResult> namedFnArgs, std::optional<ExprResult> selfArg = {}) {

        auto& fn = as<CallableDecl>(fnDecl.decl);
        auto posFnArgs = positionArguments(at(fn.params), {}, namedFnArgs, selfArg);
        if (!posFnArgs.has_value())
            return {};

        CompleteCall out;
        out.target = withDependentParametricContext(fnDecl, [&](LookupContext& context) {
            bool conversionsAllowed = fn.name != conversionWord();

            // Early type context since we don't know all the parametric arguments yet.
            // This is fine since the type expressions are evaluated in this context, which
            // have to be constant expressions.
            StaticLookupContext structCtx { &context, {} };
            if (fn.kind == DeclKind::StructDecl) {
                auto& structDecl = (StructDecl&)fn;
                structCtx.decls = at((Span<Ptr<Decl>>)structDecl.staticDecls);
            }

            if (!convertFnArgs(structCtx, at(fn.params), posFnArgs.value(), conversionsAllowed))
                return false;
            // slice self arg
            if (posFnArgs.value().size() > 0 && posFnArgs.value()[0].index() == 0
                && fn.kind == DeclKind::FnDecl && matchName(at(fn.params, 0), selfWord)) {
                VERIFY(fnDecl.staticContext()->staticDecl.valid());
                posFnArgs.value()[0] = { slice(fnDecl.staticContext()->staticDecl, posFnArgs.value()[0]), 0 };
            }

            return true;
        });
        if (!out.target.valid())
            return {};

        // Now the parametric arguments are deduced so we can make the proper context.
        // This crutial since default member initializers can access mutable static members.
        auto withCtx = [&](LookupContext& ctx) { return completeFnArgs(ctx, at(fn.params), posFnArgs.value()); };
        std::optional<std::vector<FnArgumentResult>> fnArgs;
        if (fn.kind == DeclKind::StructDecl)
            fnArgs = withCtx(*getTypeContext(out.target));
        else
            fnArgs = withParametricContext(out.target, withCtx);

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
            for (uint32_t i = 0; i < builtinImplContext.decls.size(); i++) {
                if (call.target.decl == builtinImplContext.decls[i])
                    return builtinImpls[i](this, call.args);
            }
        }

        auto& targetDecl = as<FnDecl>(call.target.decl);
        LocalLookupContext selfCtx { call.target.staticContext() };
        LocalLookupContext memberCtx { &selfCtx };
        bool hasSelf = false;
        if (call.args.size() > 0) {
            auto firstParam = at(targetDecl.params, 0);
            auto& firstArg = call.args[0].value();
            if (matchName(firstParam, selfWord)) {
                hasSelf = true;
                selfCtx.decls = { (Ptr<Decl>*)&selfDecl.decl, 1 };
                selfCtx.values = { &firstArg, 1 };
                memberCtx.decls = at(as<StructDecl>(firstArg.type.decl).params);
                memberCtx.values = firstArg.u.array->array();
            }
        }

        return withParametricContext(call.target, memberCtx, [&](LookupContext& parametricCtx) -> Value {
            ParameterLookupContext fnParamCtx { &parametricCtx };
            if (call.args.size() > 0) {
                uint32_t i = 0;
                if (hasSelf)
                    i += 1;
                fnParamCtx.decls = at(targetDecl.params).subspan(i);
                for (; i < call.args.size(); i++)
                    fnParamCtx.appendValue(call.args[i]);
            }

            BlockLookupContext assignParamCtx { &fnParamCtx };
            if (targetDecl.assignParam)
                assignParamCtx.declare(targetDecl.assignParam, assignArg);

            auto flow = evalCompoundStmt(assignParamCtx, at(targetDecl.body));

            if (call.args.size() > 0) {
                uint32_t d = hasSelf ? 1 : 0;
                for (uint32_t i = d; i < call.args.size(); i++) {
                    if (!call.args[i].isInOut)
                        continue;

                    VERIFY(setExprValue(call.args[i], fnParamCtx.values[i - d]));
                }
            }

            if (flow.kind == ControlFlowKind::None)
                return {};
            if (flow.kind == ControlFlowKind::Return)
                return flow.value;
            VERIFY_NOT_REACHED();
        });
    }
    struct CallRecord : ExprRecord {
        CompleteCall assignCall;
        CallRecord(CompleteCall assignCall)
            : ExprRecord(RecordKind::Call), assignCall(assignCall) { }
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
    ExprResult invokeCall(ExprResult baseR, std::span<const NamedExprResult> args) {
        Value base = baseR;
        auto baseType = typeOf(base);

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
                    return invalResult;
                call = std::move(c.value());
            }
            if (!call.has_value())
                return invalResult;

            return ExprResult::make<CallRecord>(evaluateCall(call.value()), call.value());
        }
        if (cmpCompleteDecls(baseType, typeType)) {
            auto type = asTypeValue(base);
            auto& decl = as<StructDecl>(type.decl);
            auto posArgs = positionArguments(at(decl.params), {}, args);
            if (!posArgs.has_value())
                return invalResult;
            LookupContext* typeCtx = getTypeContext(type);
            if (!convertFnArgs(*typeCtx, at(decl.params), posArgs.value(), true))
                return invalResult;

            auto vals = completeFnArgs(*typeCtx, at(decl.params), posArgs.value());
            if (!vals.has_value())
                return invalResult;

            CompleteCall call { type };
            call.args = std::move(vals.value());
            return ExprResult::make<CallRecord>(evaluateCall(call), call);
        }
        // function
        std::optional<ExprResult> selfArg;
        if (baseType.decl == memberOverloadSetDecl.decl) {
            VERIFY(base.kind == ValueKind::Array);
            selfArg = accessMember(baseR, 0);
            base = baseType.args()[1];
            baseType = typeOf(base);
        }
        if (cmpCompleteDecls(baseType, overloadSetType)) {
            VERIFY(base.kind == ValueKind::Array);
            std::optional<CompleteCall> call;
            std::optional<CompleteCall> assignCall;
            for (Value& overload : base.u.array->array()) {
                VERIFY(overload.kind == ValueKind::ParameterizedDecl);
                std::optional<CompleteCall> c = completeCall(overload.type, args, selfArg);
                if (!c.has_value())
                    continue;

                auto& decl = as<FnDecl>(c.value().target.decl);
                if (decl.assignParam) {
                    if (assignCall.has_value())
                        return invalResult;
                    assignCall = std::move(c.value());
                } else {
                    if (call.has_value())
                        return invalResult;
                    call = std::move(c.value());
                }
            }
            if (!call.has_value())
                return invalResult;
            Value callResult = evaluateFunction(call.value(), {});

            if (assignCall.has_value()) {
                if (!cmpCompleteCallArgs(call.value(), assignCall.value()))
                    return invalResult;

                auto assignType = withParametricContext(assignCall.value().target, [&](LookupContext& context) {
                    auto& d = as<FnDecl>(assignCall.value().target.decl);
                    return implicitlyToType(evaluateExpr(context, at(d.assignParam).type));
                });
                if (!assignType.valid() || !cmpCompleteDecls(assignType, typeOf(callResult)))
                    return invalResult;
            }
            if (assignCall.has_value())
                return ExprResult::make<CallRecord>(callResult, assignCall.value());
            else
                return ExprResult::make<BasicRecord>(callResult);
        }
        fmt::print("can not call ");
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
    LookupContext* recursiveWrapWithHasContexts(LookupContext* base, Ptr<Decl> structDecl, LookupContext& structCtx) {
        auto& d = as<StructDecl>(structDecl);
        for (auto member : at(d.params)) {
            if (at(member).kind != DeclKind::HasDecl)
                continue;

            auto& has = as<HasDecl>(member);
            Type hasType = implicitlyToType(evaluateExpr(structCtx, has.type));
            VERIFY(hasType.valid());
            auto* hasCtx = getTypeContext(hasType);
            base = &at(make<StaticRedirectLookupContext>(base, hasCtx));
            base->decls = hasCtx->decls;

            base = recursiveWrapWithHasContexts(base, hasType.decl, *hasCtx);
        }
        return base;
    }
    TypeLookupContext* getTypeContext(const Type& type) {
        for (auto& child : type.staticContext()->children) {
            if (cmpCompleteDecls(child.decl, type))
                return child.context;
        }

        auto& d = as<StructDecl>(type.decl);
        auto& withCtx = at(make<LocalLookupContext>(makeCompleteParameterContext(*type.staticContext(), at(d.with.params), type.withArgs())));
        auto& paramCtx = at(make<LocalLookupContext>(makeCompleteParameterContext(withCtx, at(d.parametric), type.args())));

        auto& typeCtx = at(make<TypeLookupContext>(&paramCtx, type));
        typeCtx.decls = at((Span<Ptr<Decl>>)d.staticDecls);
        typeCtx.parent = recursiveWrapWithHasContexts(&paramCtx, type.decl, typeCtx);

        type.staticContext()->children.push_back({ type, &typeCtx });
        return &typeCtx;
    }
    ExprResult accessMember(ExprResult base, uint32_t i) {
        auto members = at(as<StructDecl>(base.value().type.decl).params);
        return ExprResult::make<AccessRecord>(base.value().u.array->array()[i], base, members[i]);
    }
    void collectMembers(ExprResult base, Word name, std::vector<ExprResult>& results) {
        auto members = at(as<StructDecl>(base.value().type.decl).params);
        for (uint32_t i = 0; i < members.size(); i++) {
            if (matchName(members[i], name)) {
                results.push_back(accessMember(base, i));
                return;
            }
        }
        for (uint32_t i = 0; i < members.size(); i++) {
            if (at(members[i]).kind == DeclKind::HasDecl)
                collectMembers(accessMember(base, i), name, results);
        }
    }
    ExprResult evalAccessExpr(LookupContext& context, AccessExpr& e) {
        ExprResult base = evaluateExpr(context, e.base);
        if (!e.isStatic && e.member.args.count == 0) {
            std::vector<ExprResult> matches;
            collectMembers(base, e.member.word, matches);
            if (matches.size() == 1)
                return matches[0];
            EXPECT_EQ(matches.size(), 0u);
        }
        Type type = e.isStatic ? implicitlyToType(base.value()) : typeOf(base.value());
        auto* typeCtx = getTypeContext(type);

        LookupResult r = lookupIdentifier(*typeCtx, e.member, typeCtx->parametricContext);
        VERIFY(r.valid());
        DeclKind declKind = r.declKind;
        ExprResult idVal = lookupToValue(std::move(r));
        if (!idVal.value().valid())
            return idVal;

        auto badAccess = ExprResult::make<IdentifierRecord>(Value {}, LValue {});
        if (declKind == DeclKind::StructDecl && !e.isStatic)
            return badAccess;
        if (e.isStatic || declKind != DeclKind::FnDecl)
            return idVal;

        return transformMemberOverloadSet(base, idVal);
    }

    ExprResult transformMemberOverloadSet(ExprResult base, ExprResult overloadSet) {
        ParameterizedDecl setDecl = memberOverloadSetDecl;
        setDecl.allocateArgs(2);
        setDecl.args()[0] = { ExprResult::make<BasicRecord>(makeTypeValue(typeOf(base))), 0 };
        setDecl.args()[1] = { overloadSet, 1 };
        CompleteDecl setDeclComp = completeDecl(setDecl);

        std::array<NamedExprResult, 1> baseArg { { { base, {} } } };
        return invokeCall(ExprResult::make<BasicRecord>(makeTypeValue(setDeclComp)), baseArg);
    }

    ExprResult evalUnaryOperatorExpr(LookupContext& context, UnaryOperatorExpr& e) {
        ExprResult base = lookupToValue(lookupIdentifier(context, { {}, makeUnaryOpWord(e.op) }));
        NamedExprResult arg = { evaluateExpr(context, e.subExpr), {} };
        return invokeCall(base, { &arg, 1 });
    }
    ExprResult evalBinaryOperatorExpr(LookupContext& context, BinaryOperatorExpr& e) {
        if (!isCmpOp(e.op)) {
            ExprResult base = lookupToValue(lookupIdentifier(context, { {}, makeBinaryOpWord(e.op) }));
            std::array<NamedExprResult, 2> args { {
                { evaluateExpr(context, e.left), {} },
                { evaluateExpr(context, e.right), {} },
            } };
            return invokeCall(base, args);
        }
        VERIFY_NOT_REACHED();
    }
    ExprResult evalConstraintExpr(LookupContext& context, ConstraintExpr& e) {
        Value cond = evaluateExpr(context, e.constraint.condition);
        cond = deepCopy(cond);
        cond.constraint = true;
        return ExprResult::make<BasicRecord>(cond);
    }

    bool setExprValue(ExprResult base, Value value) {
        switch (base.kind()) {
        case RecordKind::Identifier: {
            auto& b = base.as<IdentifierRecord>();
            if (!b.lValue.valid())
                return false;
            setValue(b.lValue, std::move(value));
            return true;
        }
        case RecordKind::Call: {
            auto& b = base.as<CallRecord>();
            evaluateCall(b.assignCall, value);
            return true;
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
                    return setExprValue(b.base, newVal);
                }
            }
            VERIFY_NOT_REACHED();
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
        if (!stmt.op.has_value()) {
            ExprResult left = evaluateExpr(context, stmt.left);
            ExprResult right = evaluateExpr(context, stmt.right);
            VERIFY(setExprValue(left, right.value()));
        } else
            VERIFY_NOT_REACHED();
        return {};
    }

    std::string_view declName(Ptr<NamedDecl> d) {
        if (!d)
            return "Invalid Decl";
        return sview(at(d).name);
    }
    void dumpValue(const Value& value) {
        if (value.constraint)
            fmt::print("? ");
        switch (value.kind) {
        case ValueKind::Invalid:
            fmt::println("Invalid Value");
            break;
        case ValueKind::Builtin:
            fmt::println("[{}] {}", declName(value.type.decl), value.u.builtinValue);
            break;
        case ValueKind::ParameterizedDecl:
        case ValueKind::CompleteDecl:
            fmt::println("[{}] {}{}", declName(value.u.declType.decl), value.u.declType.dependent ? "dependent " : "", declName(value.type.decl));
            if (auto sDecl = asStaticDecl(value.type.decl)) {
                for (uint32_t i = 0; i < value.type.args().size(); i++) {
                    auto memberDecl = at(at(sDecl).parametric, i);
                    if (at(memberDecl).kind == DeclKind::LocalDecl)
                        fmt::print("  '{}': ", declName(memberDecl));
                    else
                        VERIFY_NOT_REACHED();
                    dumpValue(value.type.args()[i]);
                }
            }
            break;
        case ValueKind::Dependent:
            fmt::println("[{}] dependend {}", declName(value.type.decl), declName(value.u.dependent.decl));
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
        struct Type ()
        struct Array{T: Type} ()

        struct Overload ()
        struct OverloadSet ()
        struct MemberOverloadSet{T: Type, set: OverloadSet} (
            base: T;
        )

        struct TypeOverload ()
        struct TypeOverloadSet ()

        struct bool ()
        true: bool = ();
        false: bool = ();
        operation LogAnd(a: bool, b: bool) => {
            if (a) {
                if (b) return true;
            }
            return false;
        }
        operation LogOr(a: bool, b: bool) => {
            if (!a) {
                if (!b) return false;
            }
            return true;
        }
        operation LogNot(a: bool) => {
            if (a) return false;
            return true;
        }

        struct int ()
        INT_MASK: int = 0xffff'ffff'ffff'ffff;
        operation Add(i: int, j: int) => { return builtinAddAndMask(i, j, INT_MASK); }
        operation Sub(i: int, j: int) => { return i + (-j); }
        operation Mul(i: int, j: int) => { return builtinMulAndMask(i, j, INT_MASK); }
        operation Div(i: int, j: int) => { return builtinSignedDivAndMask(i, j, INT_MASK); }
        operation Neg(i: int) => { return builtinNegateAndMask(i, INT_MASK); }
    )str");
    it.findBuiltins();
    it.interpretDecls(R"str(
        fn foo(x: bool) => {
            if x
                x = foo(false);
            return x;
        }

        fn get(x&: int) => {
            x = 123;
        }
        fn callGet() => {
            mut x = 0;
            get(x);
            return x;
        }

        mut g_globalVal: int = 0;
        fn globalVal() => {
            return g_globalVal;
        }
        fn globalVal() = (n: int) {
            g_globalVal = n;
        }
        fn updateGlobalVal() => {
            get(globalVal());
            globalVal() = 456;
            return globalVal();
        }

        fn wrap{T: Type}(var: T) => {
            return var;
        }
        fn wrap{T: Type}(var&: T) = (val: T) {
            var = val;
        }
        fn updateWrappedGlobalVal() => {
            get(wrap(globalVal()));
            return wrap(globalVal());
        }

        struct constant{T: Type, v: T} (
            valueMember: T = v;
        )
        with{T: Type}
        fn mkConst{v: T}() => { return constant{T, v}(); }

        fn bar{a: int, A: Type}(b: constant{A, a}) => { return a; }
        fn bar{a: int, A: Type, b: constant{A, a}, B: Type}(c: constant{B, b}) => { return a; }
        fn bar{a: int, A: Type, b: constant{A, a}, B: Type, c: constant{B, b}, C: Type}(d: constant{C, c}) => { return a; }

        struct A (
            x: int = 0;
            y: int = 0;
        )
        fn testA() => {
            let a = A(789);
            let x: int = 1;
            let y: int = 2;
            A(x, y) = a;
            return A(x, y).x;
        }

        struct Base (
            x: int = 0;
            fn set(self&, y: int) => { x = y; }
            fn get(self) => { return x; }

            fn test(self&) => {
                self.set(7);
                return self.get();
            }
            fn test2(self&) => {
                set(8);
                return get();
            }
        )
        fn testBase() => {
            mut b = Base();
            b.test();
            return b.test2();
        }

        struct HasBase (
            has Base;
            fn callGet(self) => {
                return get();
            }
        )

        struct Flags (
            flag1: bool;
            flag2: bool;
            flag3: bool;
            flag4: bool;
        )
        fn allTrue(f: Flags) => { return f.flag1 && f.flag2 && f.flag3 && f.flag4; }
        fn atLeastOneFalse(f: Flags) => { return !allTrue(f); }
        fn conditionFlags{f ?allTrue: Flags}() => { return true; }
        fn conditionFlags{f ?atLeastOneFalse: Flags}() => { return false; }

        fn hasBase{T ?Base: Type}() => { return true; }
        fn hasBase2{b: ?Base}() => { return b; }
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
    eval("testA()");
    eval("testBase()");
    eval("HasBase().get()");
    eval("HasBase(Base(1)).x");
    eval("(5 * 4 - 2) / 3");
    eval("conditionFlags{Flags(true, true, true, false)}()");
    eval("conditionFlags{Flags(true, true, true, true)}()");
    eval("hasBase{HasBase}()");
    eval("hasBase2{A()}()");
    eval("hasBase2{HasBase()}()");
}