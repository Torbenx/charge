#pragma once

#include <sema/Program.h>

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

    Generator(glue::DeclarationNode* scope) {
        currentScope = scope;
        VERIFY(scope->parseLocation().has_value());
        tok = scope->parseLocation().value();
        program = scope->program().value();
    }

    Generator(Program* program) {
        this->program = program;
    }

    void advance();
    Expression topExpression(int_t n = 0);
    void popExpression();

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

    static Program* signatureCheck(glue::DeclarationNode* scope);
    Value fold(Value base, ExternValue v);
    Value foldImpl(Value base, Program* baseProg, std::span<const Value> parameters, ExternValue v);

    Type typeOf(Program* targetProg, ExternValue value);
    Type typeOf(Value value);
    Type verifyType(Value value);
    Program* getProgramLiteral(Value value);
    std::span<const Value> parameterizeArguments(Value value);
    static std::span<const ExternValue> parameterizeArguments(Program* targetProg, ExternValue base);

    Value generateDeclarationLiteral(glue::DeclarationNode* target);
    std::optional<Value> lookupInScope(glue::DeclarationNode* scope, Word name);
    void generateIdentifierExpr();
    void generateParameterizeExpr(int_t argumentCount);

    Value makeExpressionValue();
    Type makeProgramType(Value programLiteral);
    Value makeProgramLiteral(Program* targetProg);

    void implicitToType();
    void implicitCastTo(Type);

    void emitExpr(Node node);
    void emitValueExpr(TaggedSourceLocation<NodeKind> location, Value value);
    void emitCompoundExpr(TaggedSourceLocation<NodeKind> location, Type type, int_t childCount);
};

}