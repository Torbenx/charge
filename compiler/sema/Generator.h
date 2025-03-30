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

    static DeductionState fromFoldBase(FoldBase base) {
        DeductionState state(base.program, base.programHandle, base.arguments.size());
        for (int_t i = 0; i < (int_t)base.arguments.size(); i++)
            state.explicitArgument(i, base.arguments[i]);
        return state;
    }

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
    KIND(Namespace, Namespace*)        \
    KIND(ContainingType, TypeProgram*) \
    KIND(Local, Generator*)            \
    KIND(TemplateParameters, Program*) \
    KIND(ContainingTypeImpl, Constant) /* Always a parameterize constant */

//! Lookup context structure, main look up logic in Generator::generateIdentifierExpr()
struct LookupContext {
    enum class Kind : uint8_t {
#define KIND(name, type) name,
        ENUMERATE_LOOKUP_CONTEXT_KINDS
#undef KIND
    };

    Kind kind() const { return (Kind)(bits & TAG_MASK); }

#define KIND(name, type)                       \
    static LookupContext for##name(type arg) { \
        return LookupContext(Kind::name, arg); \
    }                                          \
    type get##name() const {                   \
        VERIFY(kind() == Kind::name);          \
        return get<type>();                    \
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
    LookupContext(Kind kind, Constant c)
        : bits(static_cast<uintptr_t>(kind) | (static_cast<uintptr_t>(c.toUint()) << 32)) {
        static_assert(sizeof(uintptr_t) == 8);
    }

    template<typename T>
    T get() const
        requires(std::is_pointer_v<T>)
    { return reinterpret_cast<T>(bits & ~TAG_MASK); }

    template<std::same_as<Constant> T>
    Constant get() const { return Constant::fromUint(bits >> 32); }

    uintptr_t bits;
};

struct Generator : Util {
    struct LocalLookupEntry {
        Word name;
        Expression data;
    };

    struct LocalReference {
        ExpressionCategory category;
        Type type;
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

        void setActive(Expression ref, bool newState) {
            switch (ref.kind()) {
            case ExpressionKind::ParameterReference:
                parameterActiveMask[ref.id()] = newState;
                break;
            case ExpressionKind::VariableReference:
                variableActiveMask[ref.id()] = newState;
                break;
            case ExpressionKind::ReferenceReference:
                referenceActiveMask[ref.id()] = newState;
                break;
            default:
                VERIFY_NOT_REACHED();
            }
        }
        bool isActive(Expression ref) const {
            switch (ref.kind()) {
            case ExpressionKind::ParameterReference:
                return parameterActiveMask[ref.id()];
            case ExpressionKind::VariableReference:
                return variableActiveMask[ref.id()];
            case ExpressionKind::ReferenceReference:
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
    OwnedExpression currentExpression = OwnedExpression(INVALID_EXPRESSION);

    Generator(Context& context, ProgramHandle handle);

    void advance();
    void setParseLocation(parse::TokenHandle);
    void clearParseLocation();

    Expression topExpression();
    OwnedExpression takeTopExpression();

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
    void visitFunctionImplDeclaration();
    void visitFunctionDeclaration();
    void visitFunctionParametersAndBody();
    void visitTypeImplDeclaration();
    void visitTypeDeclaration();
    void visitTypeMembers();

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
    Type resultType(Expression);
    Type verifyType(Constant);
    ExpressionCategory categoryOf(Expression);

    Constant makeTemplateSignature(Constant templateProg);
    Type makeTemplateIdFor(Constant templateProg);
    Constant makeFunctionSignature(Constant value);
    Expression makeGlobalReference(Constant value);
    Constant makeCopyOfOpenGlobal(Constant value);
    Constant expressionToConstant();
    Constant makeParameterize(ProgramHandle base, std::span<const Constant> arguments);
    Type typeOfNonDependentProgram(Constant value);
    Type typeOfNonDependentProgram(FoldBase base);

    Expression generateDeclarationLiteral(ScopeConstant rawValue, std::span<const Constant> parentArgs);
    Expression generateProgramLiteral(ProgramHandle progHandle, std::span<const Constant> args);
    void generateIdentifierExpr();
    void generateParameterizeExpr(std::span<const Word> argumentNames);
    CallTarget resolveCallTarget(std::span<const Word> arugmentNames);
    void generateCallExpr(SourceLocation location, CallTarget base);
    std::optional<Expression> lookupInType(TypeProgram* typeProg, std::span<const Constant> arguments, Word name);
    void generateStaticAccessExpr();
    struct MemberAccessState;
    void emitMemberAccessExpr(MemberAccessState& state);
    void generateMemberAccessExprInside(MemberAccessState& state, Type type, Word name);
    void generateMemberAccessExpr();

    Constant inheriteParameters(ScopeConstant parent);

    Expression addParameter(Word name, Type type, std::optional<Constant> defaultValue);
    Expression addExplicitParameter(Word name, Type type, std::optional<Constant> defaultValue);
    Expression addInheritedParameter(Type type, std::optional<Constant> defaultValue);
    Expression newImplicitParameter(Type type);

    Constant copyAsConstant(Expression);
    void toValueExpression(SourceLocation);

    void contextualToType(SourceLocation);
    void contextualToBool(SourceLocation);
    void initialize(SourceLocation, DeductionState&, ExpressionCategory expectCategory, ExternConstant expectedType);

    struct InstructionHandle {
        uint32_t offset;
    };
    struct ScopeInstructionHandle : InstructionHandle { };
    struct BranchInstructionHandle : ScopeInstructionHandle { };
    Instruction& at(InstructionHandle handle) {
        VERIFY(handle.offset < instructionScratch.size());
        return instructionScratch[handle.offset];
    }

    InstructionHandle emitControl(Opcode opcode, SourceLocation location, Instruction::Data data) {
        InstructionHandle ret { (uint32_t)instructionScratch.size() };
        instructionScratch.emplace_back(opcode, location, data);

        VERIFY(expressionStack.size() == 1); // There should be no expression scope
        expressionStack.back().endOffset = instructionScratch.size(); // The next expression begins after us

        return ret;
    }
    InstructionHandle emitDiscard(SourceLocation location, OwnedExpression value) {
        return emitControl(Opcode::Discard, location, { .discardValue = value.release() });
    }
    InstructionHandle emitDeactivate(SourceLocation location, Expression target) {
        return emitControl(Opcode::Deactivate, location, { .deactivateTarget = target });
    }
    InstructionHandle emitInitialize(SourceLocation location, Expression target, OwnedExpression value) {
        return emitControl(Opcode::Initialize, location, { .initialize { target, value.release() } });
    }
    BranchInstructionHandle emitBranch(SourceLocation location, OwnedExpression condition) {
        return BranchInstructionHandle { emitControl(Opcode::Branch, location, { .scope = { .u = { .branchCondition = condition.release() } } }) };
    }
    BranchInstructionHandle emitBranch(SourceLocation location, OwnedExpression condition, BranchInstructionHandle prevBranch) {
        at(prevBranch).u.scope.bodySize = (uint32_t)(instructionScratch.size() - prevBranch.offset);
        return BranchInstructionHandle { emitControl(Opcode::BranchContinued, location, { .scope = { .u = { .branchCondition = condition.release() } } }) };
    }
    ScopeInstructionHandle emitBlockScope(SourceLocation location) {
        return ScopeInstructionHandle { emitControl(Opcode::BlockScope, location, { .scope { .u = { .empty {} } } }) };
    }
    InstructionHandle emitScopeEnd(SourceLocation location, ScopeInstructionHandle scope) {
        at(scope).u.scope.bodySize = (uint32_t)(instructionScratch.size() - scope.offset);
        return emitControl(Opcode::EndScope, location, { .empty {} });
    }
    std::vector<Instruction> takeInstructions() {
        VERIFY(expressionStack.size() == 1);
        expressionStack.front().endOffset = 0;
        return std::move(instructionScratch);
    }

    void emitExpression(SourceLocation, OwnedExpression);
    void emitCall(SourceLocation, Call);
    void emitImplicitCopy(SourceLocation, ImplicitCopy);
    void declareLocalVariable(Word name, SourceLocation, VariableDeclaration);
};

}