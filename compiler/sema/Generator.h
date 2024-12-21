#pragma once

#include <sema/Util.h>

#include <FlatSet.h>

namespace sema {

struct Generator;
struct Context;

struct LookupCache {
    WordTable table;

    std::optional<Constant> get(Word name) {
        auto result = table.findWord(name);
        if (result.found)
            return Constant::fromUint(table.entries[result.bucket].payload);
        return std::nullopt;
    }

    void insert(Word name, Constant value) {
        table.insertWord(name, value.toUint());
    }
};

struct FoldBase {
    Program* program;
    ProgramHandle programHandle;
    Constant value;
    std::span<const Constant> arguments;
};

struct ConstantPair {
    ExternConstant pValue;
    Constant aValue;
};
struct ConstantPairCompare {
    std::strong_ordering operator()(Context& context, Program* argProg, Program* paramProg, ConstantPair left, ConstantPair right) const {
        auto paramOdering = Util(context, context.programHandle(argProg)).compare(left.aValue, right.aValue);
        if (paramOdering != 0)
            return paramOdering;
        return Util(context, context.programHandle(paramProg)).compare(Constant(left.pValue), Constant(right.pValue));
    }
};
using ConstantPairSet = FlatSet<ConstantPair, ConstantPairCompare, Context&, Program*, Program*>;

struct DeductionState {
    Program* program;
    ProgramHandle programHandle;
    std::vector<bool> explicitArgumentsMap;
    std::vector<Constant> arguments;
    ConstantPairSet equalities;

    DeductionState(Program* prog, ProgramHandle handle, int_t parameterCount)
        : program(prog)
        , programHandle(handle)
        , explicitArgumentsMap(parameterCount, false)
        , arguments(parameterCount, INVALID_CONSTANT) { }

    void copyParameters(int_t count) {
        for (int_t i = 0; i < count; i++)
            explicitArgument(i, Constant(ConstantKind::CopyOfParameter, i));
    }

    void explicitArgument(int_t i, Constant value) {
        VERIFY(arguments[i] == INVALID_CONSTANT);
        VERIFY(!explicitArgumentsMap[i]);
        arguments[i] = value;
        explicitArgumentsMap[i] = true;
    }

    bool isExplicitArgument(int_t index) const { return explicitArgumentsMap[index]; }

    bool isComplete() const {
        for (auto arg : arguments) {
            if (arg == INVALID_CONSTANT)
                return false;
        }
        return true;
    }

    FoldBase toFoldBase(Constant baseValue) {
        return FoldBase { program, programHandle, baseValue, arguments };
    }
};

constexpr std::vector<Constant> copyParameters(int_t parameterCount) {
    std::vector<Constant> result;
    result.reserve(parameterCount);
    for (int_t i = 0; i < parameterCount; i++)
        result.push_back(Constant(ConstantKind::CopyOfParameter, i));
    return result;
}
constexpr std::vector<Constant> copyParameters(Program* prog) {
    return copyParameters(prog->parameters.size());
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
        ExpressionResult data;
    };

    struct ReferenceUnitLock {
        uint32_t exprBits : 31;
        uint32_t sharedBit : 1;

        bool shared() const { return sharedBit != 0; }
        Reference expr() const { return std::bit_cast<Reference>(exprBits); }
    };

    struct LocalReference {
        ExpressionCategory category;
        Type type;
        std::vector<ReferenceUnitLock> locks;
    };

    struct LocalVariable {
        Type type;
    };

    struct OpaqueReference {
        Type type;
    };

    struct ValueSlotInfo {
        Type type;
    };

    enum class WildcardMeaning : uint8_t {
        Error,
        ImplicitTemplate,
    };

    struct ExpressionStackItem {
        uint32_t startOffset;
    };

    struct CallTarget {
        DeductionState state;
    };

    struct LocalState {
        std::vector<bool> parameterActiveMask;
        std::vector<bool> variableActiveMask;
        std::vector<bool> referenceActiveMask;
        // FlatSet<> trueHas;

        void setActive(Reference ref, bool newState) {
            switch (ref.kind()) {
            case ReferenceKind::Parameter:
                parameterActiveMask[ref.id()] = newState;
                break;
            case ReferenceKind::LocalVariable:
                variableActiveMask[ref.id()] = newState;
                break;
            case ReferenceKind::LocalReference:
                referenceActiveMask[ref.id()] = newState;
                break;
            default:
                VERIFY_NOT_REACHED();
            }
        }
        bool isActive(Reference ref) const {
            switch (ref.kind()) {
            case ReferenceKind::Parameter:
                return parameterActiveMask[ref.id()];
            case ReferenceKind::LocalVariable:
                return variableActiveMask[ref.id()];
            case ReferenceKind::LocalReference:
                return referenceActiveMask[ref.id()];
            default:
                VERIFY_NOT_REACHED();
            }
        }

        bool operator==(const LocalState&) const = default;
    };

    struct LocalScope {
        uint32_t localScopeDepth = 0;
        uint32_t localVariableCount = 0;
        uint32_t localReferenceCount = 0;
    };

    using Token = parse::TokenKind;

    const parse::TokenInfo* tok = nullptr;

    std::vector<Instruction*> instructionScratch;
    std::vector<ExpressionStackItem> expressionStack;
    std::vector<Type> parameterTypes;
    std::vector<LookupContext> lookupStack;
    std::vector<LocalLookupEntry> localLookupEntries;
    std::vector<LocalVariable> localVariables;
    std::vector<LocalReference> localReferences;
    std::vector<OpaqueReference> opaqueReferences;
    std::vector<ValueSlotInfo> valueSlots;
    LocalState localState;
    WildcardMeaning wildcardMeaning = WildcardMeaning::Error;
    uint32_t localScopeDepth = 0;
    OwnedExpressionResult currentExpression = OwnedExpressionResult(INVALID_EXPRESSION_RESULT);

    Generator(Context& context, ProgramHandle handle);

    void advance();
    void setParseLocation(parse::TokenHandle);
    void clearParseLocation();

    ExpressionResult topExpression();
    OwnedExpressionResult takeTopExpression();

    LocalScope beginLocalScope(SourceLocation);
    void endLocalScope(LocalScope scope, SourceLocation);

    void visitDeclaration();
    void visitTemplateParameters();
    struct VariableDeclaration {
        Type type;
        bool hasInitializer;
    };
    VariableDeclaration visitVariableDeclaration(ExpressionCategory expectedCategory, bool programParameters);
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

    FoldBase asFoldBase(Constant value);
    std::optional<FoldBase> tryAsFoldBase(Constant value);
    Constant fold(Constant base, ExternConstant v);
    Constant fold(FoldBase base, ExternConstant v);
    bool staticMatch(DeductionState& state, ExternConstant pValue, Constant aValue);

    RuntimeParameter member(MemberPointer pointer);
    Type memberType(MemberPointer pointer);
    Type memberType(Constant memberPointer);
    Type typeOf(Constant);
    Type resultType(ExpressionResult);
    Type verifyType(Constant);
    Type referencedType(Reference);
    ExpressionCategory categoryOf(ExpressionResult);

    Constant makeTemplateSignature(Constant templateProg);
    Type makeTemplateIdFor(Constant templateProg);
    Constant makeFunctionSignature(Constant value);
    Constant expressionToConstant();
    Constant makeParameterize(ProgramHandle base, std::span<const Constant> arguments);
    Type typeOfNonDependentProgram(Constant value);
    Type typeOfNonDependentProgram(FoldBase base);

    Constant generateDeclarationLiteral(ScopeConstant rawValue, std::span<const Constant> parentArgs);
    void generateIdentifierExpr();
    void generateParameterizeExpr(std::span<const Word> argumentNames);
    CallTarget resolveCallTarget(std::span<const Word> arugmentNames);
    void generateCallExpr(CallTarget base);
    std::optional<Constant> lookupInType(TypeProgram* typeProg, std::span<const Constant> arguments, Word name);
    void generateStaticAccessExpr();
    struct MemberAccessState;
    void emitMemberAccessExpr(MemberAccessState& state);
    void generateMemberAccessExprInside(MemberAccessState& state, Type type, Word name);
    void generateMemberAccessExpr();

    Constant inheriteParameters(ScopeConstant parent);

    Reference addParameter(Word name, Type type, std::optional<Constant> defaultValue);
    Reference addExplicitParameter(Word name, Type type, std::optional<Constant> defaultValue);
    Reference addInheritedParameter(Type type, std::optional<Constant> defaultValue);
    Reference newImplicitParameter(Type type);

    void toValueExpression(SourceLocation);

    void contextualToType(SourceLocation);
    void contextualToBool(SourceLocation);
    void initialize(SourceLocation, DeductionState&, ExpressionCategory expectCategory, ExternConstant expectedType);

    OwnedValueSlot newValueSlot(Type type) {
        auto id = valueSlots.size();
        valueSlots.push_back({ type });
        return OwnedValueSlot(id);
    }

    template<typename T, typename... Args>
    T* allocateInstruction(Args&&... args) {
        return context.instructionAllocator.allocate<T>(std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T* emitControl(Args&&... args) {
        T* ptr = context.instructionAllocator.allocate<T>(std::forward<Args>(args)...);
        instructionScratch.push_back(ptr);
        return ptr;
    }

    void emitInstructionExpr(Instruction*, OwnedExpressionResult);
    void emitConstantExpr(SourceLocation, Constant);
    void emitReferenceExpr(SourceLocation, Reference);
    void declareLocalVariable(Word name, SourceLocation, VariableDeclaration);
};

}