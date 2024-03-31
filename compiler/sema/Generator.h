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

    enum class WildcardMeaning {
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
    std::vector<Node> scratchBlock;
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

    static Program* signatureCheck(glue::DeclarationNode* scope);

    Type typeOfValue(Value value);

    Type verifyType(Value value);

    Value generateDeclarationLiteral(glue::DeclarationNode* target);
    std::optional<Value> lookupInScope(glue::DeclarationNode* scope, Word name);
    void generateIdentifierExpr();

    void buildDependentParents();

    Value makeExpressionValue();
    Type makeProgramType(Program* program);
    Value makeProgramLiteral(Program* program);
    Value makeStaticAccess(Value base, Program* program, ExternValue value);

    void implicitToType();
    void implicitCastTo(Type);

    void emitExpr(Node node);
    void emitValueExpr(TaggedSourceLocation<NodeKind> location, Value value);
    void emitCompoundExpr(TaggedSourceLocation<NodeKind> location, Type type, int_t childCount);
};

}