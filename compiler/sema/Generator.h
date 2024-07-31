#pragma once

#include <sema/Program.h>

namespace sema {

struct Generator;
struct Context;

struct LookupCache {
    WordTable table;

    std::optional<Value> get(Word name) {
        auto result = table.findWord(name);
        if (result.found)
            return Value::fromUint(table.entries[result.bucket].payload);
        return std::nullopt;
    }

    void insert(Word name, Value value) {
        table.insertWord(name, value.toUint());
    }
};

struct FoldBase {
    Program* program;
    ProgramHandle programHandle;
    Value value;
    std::vector<Value> arguments;
};

struct DeductionState {
    struct ExpressionMatch {
        ExternValue pValue;
        Value aValue;
    };
    Program* program;
    ProgramHandle programHandle;
    std::vector<bool> explicitArgumentsMap;
    std::vector<Value> arguments;
    std::vector<ExpressionMatch> expressionMatches;

    DeductionState(Program* prog, ProgramHandle handle, int_t parameterCount)
        : program(prog)
        , programHandle(handle)
        , explicitArgumentsMap(parameterCount, false)
        , arguments(parameterCount, INVALID_VALUE) { }

    void identityMap(int_t count) {
        for (int_t i = 0; i < count; i++)
            explicitArgument(i, Value(ValueKind::Parameter, i));
    }

    void explicitArgument(int_t i, Value value) {
        VERIFY(arguments[i] == INVALID_VALUE);
        VERIFY(!explicitArgumentsMap[i]);
        arguments[i] = value;
        explicitArgumentsMap[i] = true;
    }

    bool isExplicitArgument(int_t index) const { return explicitArgumentsMap[index]; }

    bool isComplete() const {
        for (auto arg : arguments) {
            if (arg == INVALID_VALUE)
                return false;
        }
        return true;
    }

    FoldBase toFoldBase(Value baseValue) {
        return FoldBase { program, programHandle, baseValue, arguments };
    }
};

constexpr std::vector<Value> identityParameterMap(int_t parameterCount) {
    std::vector<Value> result;
    result.reserve(parameterCount);
    for (int_t i = 0; i < parameterCount; i++)
        result.push_back(Value(ValueKind::Parameter, i));
    return result;
}
constexpr std::vector<Value> identityParameterMap(Program* prog) {
    return identityParameterMap(prog->parameters.size());
}

#define ENUMERATE_LOOKUP_CONTEXT_KINDS \
    KIND(Namespace, Namespace)         \
    KIND(Type, TypeProgram)            \
    KIND(Local, Generator)             \
    KIND(TemplateParameters, Program)

struct LookupContext {
    enum class Kind : uint8_t {
#define KIND(name, type) name,
        ENUMERATE_LOOKUP_CONTEXT_KINDS
#undef KIND
    };

    Kind kind() const { return (Kind)(bits & TAG_MASK); }

#define KIND(name, type)                        \
    static LookupContext for##name(type* arg) { \
        return LookupContext(Kind::name, arg);  \
    }                                           \
    type* get##name() const {                   \
        VERIFY(kind() == Kind::name);           \
        return ptr<type>();                     \
    }
    ENUMERATE_LOOKUP_CONTEXT_KINDS
#undef KIND

private:
    static constexpr uintptr_t TAG_MASK = 7;

    LookupContext(Kind kind, void* ptr)
        : bits(static_cast<uintptr_t>(kind) | reinterpret_cast<uintptr_t>(ptr)) {
        VERIFY((static_cast<uintptr_t>(kind) & ~TAG_MASK) == 0u);
        VERIFY((reinterpret_cast<uintptr_t>(ptr) & TAG_MASK) == 0u);
    }

    template<typename T>
    T* ptr() const { return reinterpret_cast<T*>(bits & ~TAG_MASK); }

    uintptr_t bits;
};

struct Generator {
    struct LocalLookupEntry {
        Word name;
        Value value;

        LocalLookupEntry(Word name, int_t localValueIndex)
            : name(name), value(ValueKind::Invalid, localValueIndex) { }
        LocalLookupEntry(Word name, Value constant)
            : name(name), value(constant) { VERIFY(constant.kind() == ValueKind::Invalid); }

        bool isLocalValue() const {
            return value.kind() == ValueKind::Invalid;
        }
        int_t localValueIndex() const {
            VERIFY(isLocalValue());
            return value.id();
        }
        Value constant() const {
            VERIFY(!isLocalValue());
            return value;
        }
    };

    struct LocalValue {
        Type type;
    };

    enum class WildcardMeaning : uint8_t {
        Error,
        ImplicitTemplate,
    };

    struct StackItem {
        uint32_t nodeIndex;
    };

    struct CallTarget {
        DeductionState state;
    };

    struct ScopedChange {
        std::optional<Generator*> g;
        ScopedChange() = default;
        ScopedChange(Generator* g)
            : g(g) { }
        ScopedChange(const ScopedChange&) = delete;
        ScopedChange(ScopedChange&& other)
            : g(other.g) { other.g = std::nullopt; }
        ScopedChange& operator=(const ScopedChange&) = delete;
        ScopedChange& operator=(ScopedChange&& other) {
            g = other.g;
            other.g = std::nullopt;
            return *this;
        }
    };

    struct ParseScope : ScopedChange {
        ParseScope(Generator* g, parse::TokenHandle parseLocation)
            : ScopedChange(g) {
            g->tok = &g->context.parseOutput.tokens[parseLocation.id()];
        }
        ~ParseScope() {
            if (g.has_value())
                g->tok = nullptr;
        }
    };

    using Token = parse::TokenKind;

    Context& context;
    const parse::TokenInfo* tok = nullptr;
    Program* program = nullptr;
    ProgramHandle programHandle;

    std::vector<Node> nodeScratch;
    std::vector<StackItem> nodeStack;
    std::vector<Type> parameterTypes;
    std::vector<LookupContext> lookupStack;
    std::vector<LocalLookupEntry> localLookupEntries;
    std::vector<LocalValue> localValues;
    WildcardMeaning wildcardMeaning = WildcardMeaning::Error;

    Generator(Context& context, ProgramHandle handle);

    void advance();
    Node* topNode(int_t n = 0);
    Expression topExpression(int_t n = 0);
    void popNode();
    void popNodes(int_t n);

    void visitDeclaration();
    void visitTemplateParameters();
    struct VariableDeclaration {
        Type type;
        bool hasInitializer;
    };
    VariableDeclaration visitVariableDeclaration(bool deduceFromInitializer);
    Program::Parameter visitTemplateParameter();
    void visitStaticVariableDeclaration();
    void visitFunctionDeclaration();
    void visitTypeDeclaration();

    void visitStatement();
    void visitExpression();
    void visitBinaryExpr();
    void visitUnaryExpr();
    void visitPostfixExpr();
    void visitPrimaryExpr();
    int_t visitExpressionList();

    static void signatureCheck(Context& context, ProgramHandle progHandle);
    static void generateBuiltins(Context& context);

    FoldBase asFoldBase(Value value);
    std::optional<FoldBase> tryAsFoldBase(Value value);
    Value fold(Value base, ExternValue v);
    Value fold(FoldBase base, ExternValue v);
    bool staticMatch(DeductionState& state, ExternValue pValue, Value aValue);
    FoldBase selfFold();
    DeductionState selfDeduction();

    RuntimeParameter member(MemberPointer pointer);
    Type memberType(MemberPointer pointer);
    Type typeOf(Value value);
    Type verifyType(Value value);

    Value makeTemplateSignature(Value templateProg);
    Type makeTemplateIdFor(Value templateProg);
    Value makeFunctionSignature(Value value);
    Value makeExpressionValue();
    Value makeExpressionValue(Expression expr);
    Value makeParameterize(ProgramHandle base, std::span<const Value> arguments);
    Type typeOfNonDependentProgram(Value value);
    Type typeOfNonDependentProgram(FoldBase base);

    Value generateDeclarationLiteral(ScopeValue rawValue, std::span<const Value> parentArgs);
    void generateIdentifierExpr();
    void generateParameterizeExpr(int_t argumentCount);
    CallTarget resolveCallTarget();
    void generateCallExpr(CallTarget base, int_t argumentCount);
    std::optional<Value> lookupInType(TypeProgram* typeProg, std::span<const Value> arguments, Word name);
    void generateStaticAccessExpr();
    struct MemberAccessState;
    void emitMemberAccessExpr(MemberAccessState& state);
    void generateMemberAccessExprInside(MemberAccessState& state, Type type, Word name);
    void generateMemberAccessExpr();

    Value inheriteParameters(ScopeValue parent);

    Value addParameter(Word name, Type type, std::optional<Value> defaultValue);
    Value addExplicitParameter(Word name, Type type, std::optional<Value> defaultValue);
    Value addInheritedParameter(Type type, std::optional<Value> defaultValue);
    Value newImplicitParameter(Type type);

    void contextualType();
    void contextualBool();
    void implicitCastTo(DeductionState& state, ExternValue);
    void implicitCastTo(DeductionState& state, ExternValue pType, Expression arg);

    void emitNode(NodeKind kind, SourceLocation location, int_t childCount, NodeData data);
    void emitExpr(NodeKind kind, SourceLocation location, int_t childCount, Type type, ExprData data);
    void emitConstantExpr(SourceLocation location, Value value);
    void emitReferenceExpr(SourceLocation location, int_t localValueIndex);
    void declareLocal(Word name, SourceLocation location, VariableDeclaration decl);
};

}