#pragma once

#include <sema/Program.h>

namespace glue {
struct Context;
}

namespace sema {

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

struct Generator {
    struct LocalDeclarationEntry {
        Word name;
        Value value;
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

    struct CallBase {
        DeductionState state;
    };

    using Token = parse::TokenKind;

    glue::Context& context;
    glue::DeclarationNode* currentScope = nullptr;
    const parse::TokenInfo* tok = nullptr;
    Program* program = nullptr;
    ProgramHandle programHandle;

    LookupCache lookupCache;
    std::vector<Value> dependentParents;
    std::vector<LocalDeclarationEntry> localDeclarations;
    std::vector<LocalValue> localValues;
    std::vector<Node> nodeScratch;
    std::vector<StackItem> nodeStack;
    std::vector<Type> parameterTypes;
    WildcardMeaning wildcardMeaning = WildcardMeaning::Error;

    Generator(glue::Context& context, ProgramHandle handle);
    Generator(glue::Context& context, glue::DeclarationNode* scope);

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

    void visitExpression();
    void visitBinaryExpr();
    void visitUnaryExpr();
    void visitPostfixExpr();
    void visitPrimaryExpr();
    int_t visitExpressionList();

    static ProgramHandle signatureCheck(glue::Context& context, glue::DeclarationNode* scope);
    static void generateBuiltins(glue::Context& context);
    FoldBase asFoldBase(Value value);
    std::optional<FoldBase> tryAsFoldBase(Value value);
    Value fold(Value base, ExternValue v);
    Value fold(FoldBase base, ExternValue v);
    bool staticMatch(DeductionState& state, ExternValue pValue, Value aValue);
    FoldBase selfFold();
    DeductionState selfDeduction();

    Type typeOf(Value value);
    Type verifyType(Value value);
    std::span<Value> parameterizeArguments(Value value);
    static std::span<const ExternValue> parameterizeArguments(Program* targetProg, ExternValue base);

    Value generateDeclarationLiteral(glue::DeclarationNode* target);
    std::optional<Value> lookupInScope(glue::DeclarationNode* scope, Word name);
    void generateIdentifierExpr();
    void generateParameterizeExpr(int_t argumentCount);
    CallBase resolveCallBase();
    void generateCallExpr(CallBase base, int_t argumentCount);

    Value makeExpressionValue();
    Value makeExpressionValue(Expression expr);
    Value makeProgramValue(ProgramHandle targetHandle);
    Type makeTemplateIdFor(ProgramHandle targetHandle);
    Type typeOfNonDependentProgram(Value value);
    Type typeOfNonDependentProgram(FoldBase base);
    Value makeParameterize(ProgramHandle base, std::span<const Value> arguments);
    void buildParent(glue::DeclarationNode* parentDeclaration);
    void buildSelf();

    Value addParameter(Word name, Type type, std::optional<Value> defaultValue);
    Value addExplicitParameter(Word name, Type type, std::optional<Value> defaultValue);
    Value addInheritedParameter(Type type, std::optional<Value> defaultValue);
    Value newImplicitParameter(Type type);

    void implicitToType();
    void implicitCastTo(DeductionState& state, ExternValue);
    Value implicitCastTo(DeductionState& state, ExternValue pType, Expression arg);

    void emitNode(NodeKind kind, SourceLocation location, int_t childCount, NodeData data);
    void emitExpr(NodeKind kind, SourceLocation location, int_t childCount, Type type, ExprData data);
    void emitConstantExpr(SourceLocation location, Value value);
};

}