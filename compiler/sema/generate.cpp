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

    enum class WildcardMeaning {
        Error,
        ImplicitTemplate,
    };

    struct StackItem {
        uint32_t nodeIndex;
    };

    using Token = parse::TokenKind;

    glue::DeclarationNode* currentScope = nullptr;
    parse::TokenInfo* tok = nullptr;
    Program* program = nullptr;

    LookupCache lookupCache;
    std::vector<LocalDeclarationEntry> localDeclarations;
    BasicBlock scratchBlock;
    std::vector<StackItem> expressionStack;
    WildcardMeaning wildcardMeaning = WildcardMeaning::Error;

    Generator(glue::DeclarationNode* scope) {
        currentScope = scope;
        tok = nullptr; // scope->parseLocation();
        program = scope->program().value();
    }

    Node* takeExpression();

    void advance() { tok += 1; }

    void visitDeclaration() {
        if (tok->kind() == Token::TemplateAttribute) {
            visitTemplateParameters();
        }
    }

    void visitTemplateParameters() {
        while (tok->kind() != Token::EmptyNode) {
            visitTemplateParameter();
        }
    }

    void visitTemplateParameter() {
        if (tok->kind() != Token::ImplicitKindParameter) {
            // report error
            VERIFY_NOT_REACHED();
        }
        Word name = Word::fromUint(tok->data());
        advance();

        Type type;
        if (tok->kind() != Token::AssignStmt) {
            // parse type
            wildcardMeaning = WildcardMeaning::ImplicitTemplate;
            visitExpression();
            implicitToType();
            type = verifyType(program->addExpression(takeExpression()));
            wildcardMeaning = WildcardMeaning::Error;
        } else {
            type = verifyType(program->addImplicitParameter(builtins::type_type));
        }
        VERIFY(tok->kind() == Token::AssignStmt);
        advance();

        std::optional<Value> initializer;
        if (tok->kind() != Token::ExpressionStmt) {
            // parse initializer
            visitExpression();
            implicitCastTo(type);
            initializer = program->addExpression(takeExpression());
        }
        VERIFY(tok->kind() == Token::ExpressionStmt);
        advance();

        program->addParameter(name, type, initializer);
    }

    void visitExpression() {
        visitBinaryExpr();
    }

    void visitBinaryExpr() {
        visitUnaryExpr();
    }

    void visitUnaryExpr() {
        if (parse::isUnaryExpr(tok->kind())) {
            // create placeholder node for the function
            advance();
            visitUnaryExpr();
            // resolve and create call expression
        } else {
            visitPostfixExpr();
        }
    }

    void visitPostfixExpr() {
        visitPrimaryExpr();
    }

    void visitPrimaryExpr() {
        if (tok->kind() == Token::IdentifierExpr) {
            generateIdentifierExpr();
            advance();
        } else {
            VERIFY_NOT_REACHED();
        }
    }

    Program* signatureCheck(glue::DeclarationNode* scope) {
        auto scopeProg = scope->program();
        if (scopeProg.has_value() && scopeProg->status() >= ProgramStatus::SignatureChecked)
            return scopeProg;

        if (!scopeProg.has_value())
            scope->setProgram(new Program());
        Generator generator(scope);
    }

    Type typeOfValue(Value value);
    Type typeOfProgram(Program*);

    Type verifyType(Value value) {
        VERIFY(typeOfValue(value) == builtins::type_type);
        return (Type)value;
    }

    Value generateDeclarationLiteral(glue::DeclarationNode* target);
    std::optional<Value> lookupInScope(glue::DeclarationNode* scope, Word name);

    void generateIdentifierExpr();

    void implicitToType() { implicitCastTo(builtins::type_type); }
    void implicitCastTo(Type);
};

std::optional<Value> Generator::lookupInScope(glue::DeclarationNode* scope, Word name) {
    using Kind = glue::DeclarationNode::Kind;
    if (scope->kind() == Kind::Namespace) {
        auto child = scope->findChild(name);
        if (child.has_value())
            return generateDeclarationLiteral(child);
        return std::nullopt;
    }
    VERIFY_NOT_REACHED();
}

Value Generator::generateDeclarationLiteral(glue::DeclarationNode* target) {
    using Kind = glue::DeclarationNode::Kind;
    switch (target->kind()) {
    case Kind::Namespace:
        return program->addLiteral(builtins::namespace_type, target);
    case Kind::Type:
    case Kind::Function:
    case Kind::Variable: {
        Program* targetProg = signatureCheck(target);
        return program->addLiteral(typeOfProgram(targetProg), targetProg);
    }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generateIdentifierExpr() {
    Word name = Word::fromUint(tok->data());
    for (const auto& entry : localDeclarations) {
        if (name == entry.name) {
            scratchBlock.emitValueExpr({ NodeKind::ReferenceExpr, tok->location() }, entry.value);
            return;
        }
    }
    auto result = lookupCache.get(name);
    if (result.has_value()) {
        scratchBlock.emitValueExpr({ NodeKind::ConstantExpr, tok->location() }, result.value());
        return;
    }
    glue::DeclarationNode* lookupScope = currentScope;
    while (lookupScope != nullptr) {
        auto lookup = lookupInScope(lookupScope, name);
        if (lookup.has_value()) {
            lookupCache.insert(name, lookup.value());
            scratchBlock.emitValueExpr({ NodeKind::ConstantExpr, tok->location() }, lookup.value());
            return;
        }
        lookupScope = lookupScope->declaringNode();
    }
    VERIFY_NOT_REACHED();
}

}