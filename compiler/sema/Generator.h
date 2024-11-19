#pragma once

#include <sema/Util.h>

#include <FlatSet.h>

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
    std::span<const Value> arguments;
};

struct ValuePair {
    ExternValue pValue;
    Value aValue;
};
struct ValuePairCompare {
    std::strong_ordering operator()(Context& context, Program* argProg, Program* paramProg, ValuePair left, ValuePair right) const {
        auto paramOdering = Util(context, context.programHandle(argProg)).compare(left.aValue, right.aValue);
        if (paramOdering != 0)
            return paramOdering;
        return Util(context, context.programHandle(paramProg)).compare(Value(left.pValue), Value(right.pValue));
    }
};
using ValuePairSet = FlatSet<ValuePair, ValuePairCompare, Context&, Program*, Program*>;

struct DeductionState {
    Program* program;
    ProgramHandle programHandle;
    std::vector<bool> explicitArgumentsMap;
    std::vector<Value> arguments;
    ValuePairSet equalities;

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

struct Generator : Util {
    struct LocalLookupEntry {
        Word name;
        ValueOrReferenceExpression data;
    };

    struct ReferenceUnitLock {
        uint32_t exprBits : 31;
        uint32_t sharedBit : 1;

        bool shared() const { return sharedBit != 0; }
        ReferenceExpression expr() const { return std::bit_cast<ReferenceExpression>(exprBits); }
    };

    struct LocalReference {
        Type type;
        std::vector<ReferenceUnitLock> locks;
    };

    struct LocalVariable {
        Type type;
    };

    enum class WildcardMeaning : uint8_t {
        Error,
        ImplicitTemplate,
    };

    struct ExpressionStackItem {
        uint32_t endOffset;
    };

    struct CallTarget {
        DeductionState state;
    };

    struct LocalState {
        std::vector<bool> parameterActiveMask;
        std::vector<bool> variableActiveMask;
        std::vector<bool> referenceActiveMask;
        // FlatSet<> trueHas;

        void setActive(ReferenceExpression ref, bool newState) {
            switch (ref.kind()) {
            case ReferenceExpressionKind::Parameter:
                parameterActiveMask[ref.id()] = newState;
                break;
            case ReferenceExpressionKind::LocalVariable:
                variableActiveMask[ref.id()] = newState;
                break;
            case ReferenceExpressionKind::LocalReference:
                referenceActiveMask[ref.id()] = newState;
                break;
            default:
                VERIFY_NOT_REACHED();
            }
        }
        bool isActive(ReferenceExpression ref) const {
            switch (ref.kind()) {
            case ReferenceExpressionKind::Parameter:
                return parameterActiveMask[ref.id()];
            case ReferenceExpressionKind::LocalVariable:
                return variableActiveMask[ref.id()];
            case ReferenceExpressionKind::LocalReference:
                return referenceActiveMask[ref.id()];
            default:
                VERIFY_NOT_REACHED();
            }
        }

        bool operator==(const LocalState&) const = default;
    };

    struct JumpReference {
        uint32_t offset;
        LocalState originState;
    };
    struct JumpLabel {
        uint32_t offset;
        LocalState targetState;
    };

    struct LocalScope {
        uint32_t localScopeDepth = 0;
        uint32_t localVariableCount = 0;
        uint32_t localReferenceCount = 0;
    };

    using Token = parse::TokenKind;

    const parse::TokenInfo* tok = nullptr;

    std::vector<Instruction> instructionScratch;
    std::vector<ExpressionStackItem> expressionStack = { ExpressionStackItem { .endOffset = 0 } };
    std::vector<Type> parameterTypes;
    std::vector<LookupContext> lookupStack;
    std::vector<LocalLookupEntry> localLookupEntries;
    std::vector<LocalVariable> localVariables;
    std::vector<LocalReference> localReferences;
    LocalState localState;
    WildcardMeaning wildcardMeaning = WildcardMeaning::Error;
    uint32_t localScopeDepth = 0;

    Generator(Context& context, ProgramHandle handle);

    void advance();
    void setParseLocation(parse::TokenHandle);
    void clearParseLocation();

    Instruction& topInstruction(int_t n = 0);
    Expression topExpression(int_t n = 0);
    void popExpression();
    void popExpressions(int_t n);

    LocalScope beginLocalScope(SourceLocation);
    void endLocalScope(LocalScope scope, SourceLocation);

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
    Type memberType(Value memberPointerValue);
    Type typeOf(Value);
    Type verifyType(Value value);
    Type referencedType(ReferenceExpression);

    Value makeTemplateSignature(Value templateProg);
    Type makeTemplateIdFor(Value templateProg);
    Value makeFunctionSignature(Value value);
    Value makeExpressionValue();
    Value makeParameterize(ProgramHandle base, std::span<const Value> arguments);
    Type typeOfNonDependentProgram(Value value);
    Type typeOfNonDependentProgram(FoldBase base);

    Value generateDeclarationLiteral(ScopeValue rawValue, std::span<const Value> parentArgs);
    void generateIdentifierExpr();
    void generateParameterizeExpr(std::span<const Word> argumentNames);
    CallTarget resolveCallTarget(std::span<const Word> arugmentNames);
    void generateCallExpr(CallTarget base);
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

    void contextualToType();
    void contextualToBool();
    void implicitCastTo(DeductionState& state, ExternValue);

    void emitControl(Opcode, SourceLocation, int_t childCount, InstructionData data);
    void emitExpression(Opcode, SourceLocation, int_t childCount, Type type, ExpressionData data);
    void emitConstantExpr(SourceLocation, Value value);
    void emitReferenceExpr(SourceLocation, ReferenceExpression);
    void declareLocalVariable(Word name, SourceLocation, VariableDeclaration);

    void emitJumpTo(Opcode, SourceLocation, int_t childCount, const JumpLabel&);
    JumpReference emitJump(Opcode, SourceLocation, int_t childCount);
    void linkToNextInstruction(const JumpReference& jump);
    JumpLabel nextInstruction();
    void link(int_t originOffset, int_t targetOffset, const LocalState& originState, const LocalState& targetState);
    void emitJumpTo(SourceLocation location, const JumpLabel& label) { emitJumpTo(Opcode::Jump, location, 0, label); }
    JumpReference emitJump(SourceLocation location) { return emitJump(Opcode::Jump, location, 0); }
    void emitJumpIfTo(SourceLocation location, const JumpLabel& label) { emitJumpTo(Opcode::JumpIf, location, 1, label); }
    JumpReference emitJumpIf(SourceLocation location) { return emitJump(Opcode::JumpIf, location, 1); }
};

}