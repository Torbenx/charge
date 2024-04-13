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

struct DeductionState {
    struct ExpressionMatch {
        ExternValue pValue;
        Value aValue;
    };
    std::vector<bool> explicitArgumentsMap;
    std::vector<ExpressionMatch> expressionMatches;

    DeductionState(int_t parameters) { explicitArgumentsMap.resize(parameters); }

    bool isExplicitArgument(int_t index) { return explicitArgumentsMap[index]; }
};

struct BaseProgram {
    Program* program;
    Value value;
    std::span<Value> arguments;

    Program* operator->() const { return program; }
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

    using Token = parse::TokenKind;

    glue::Context& context;
    glue::DeclarationNode* currentScope = nullptr;
    const parse::TokenInfo* tok = nullptr;
    Program* program = nullptr;

    LookupCache lookupCache;
    std::vector<Value> dependentParents;
    std::vector<LocalDeclarationEntry> localDeclarations;
    std::vector<LocalValue> localValues;
    std::vector<Node> expressionScratch;
    std::vector<StackItem> expressionStack;
    WildcardMeaning wildcardMeaning = WildcardMeaning::Error;

    Generator(glue::Context& context, Program* program);
    Generator(glue::Context& context, glue::DeclarationNode* scope);

    void advance();
    Expression topExpression(int_t n = 0);
    void popExpression();
    void popExpressions(int_t n);

    void visitDeclaration();
    void visitTemplateParameters();
    struct VariableDeclaration {
        Type type;
        std::optional<Value> initializer;
    };
    VariableDeclaration visitVariableDeclaration();
    void visitTemplateParameter();
    void visitStaticVariableDeclaration();

    void visitExpression();
    void visitBinaryExpr();
    void visitUnaryExpr();
    void visitPostfixExpr();
    void visitPrimaryExpr();
    int_t visitExpressionList();

    static ProgramHandle signatureCheck(glue::Context& context, glue::DeclarationNode* scope);
    static void generateBuiltins(glue::Context& context);
    BaseProgram asProgram(Value value);
    Value fold(Value base, ExternValue v);
    Value fold(BaseProgram base, ExternValue v);
    bool staticMatch(DeductionState& state, ExternValue pValue, Program* pBase, std::span<Value> arguments, Value aValue);

    Type typeOf(Value value);
    Type verifyType(Value value);
    std::optional<Program*> getProgramLiteral(Value value);
    std::span<Value> parameterizeArguments(Value value);
    static std::span<const ExternValue> parameterizeArguments(Program* targetProg, ExternValue base);

    Value generateDeclarationLiteral(glue::DeclarationNode* target);
    std::optional<Value> lookupInScope(glue::DeclarationNode* scope, Word name);
    void generateIdentifierExpr();
    void generateParameterizeExpr(int_t argumentCount);

    Value makeExpressionValue();
    Value makeExpressionValue(Expression expr);
    Value makeProgramValue(ProgramHandle targetHandle);
    Type makeTemplateIdFor(ProgramHandle targetHandle);
    void buildParent(glue::DeclarationNode* parentDeclaration);

    void implicitToType();
    void implicitCastTo(Type);
    void implicitCastTo(DeductionState& state, ExternValue pType, Program* pBase, std::span<Value> arguments, Expression arg);

    void emitExpr(Node node);
    void emitValueExpr(TaggedSourceLocation<NodeKind> location, Value value);
    void emitCompoundExpr(TaggedSourceLocation<NodeKind> location, Type type, int_t childCount);
};

}