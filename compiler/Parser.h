#pragma once

#include "expr.h"

struct SourcePosition {
    uint32_t line;
    uint32_t column;
};
struct StreamToken {
    uint32_t tokenBits : 8;
    uint32_t field1Bits : 24;
    uint32_t beginBits;
    Token token() const { return (Token)tokenBits; }
    uint32_t begin() const { return beginBits; }
    uint32_t lineNumber() const {
        VERIFY(token() == Token::Newline);
        return field1Bits;
    }
    uint32_t length() const {
        VERIFY(token() != Token::Newline);
        return field1Bits;
    }
    uint32_t end() const { return begin() + length(); }
    static StreamToken makeNewline(uint32_t lineNumber, uint32_t begin) {
        return StreamToken { std::to_underlying(Token::Newline), lineNumber, begin };
    }
    static StreamToken make(Token token, uint32_t begin, uint32_t end) {
        return StreamToken { std::to_underlying(token), end - begin, begin };
    }
};

struct LexerState : TokenWithData {
    // These two fields should only be accessed by the Lexer interals.
    // To get the range of the current token the tokenStream should be used
    // as after reemitLastToken() these will not have the correct values.
    uint32_t tokBegin = 0;
    uint32_t sourceOffset = 0;

    uint32_t lineNumber = 1;

    TokenWithData cachedNextToken = {};

    std::string_view sourceBuffer = {};
    HomogeneousStreamAllocator<StreamToken> tokenStream;

    LexerState(std::string_view sourceBuffer)
        : sourceBuffer(sourceBuffer) {
        if (!sourceBuffer.empty())
            tokenStream.emit(StreamToken::makeNewline(1, 0));
    }
};

struct Lexer : LexerState {
    struct ErrorHandler {
        enum class LexerAction {
            // try to lex a new token at the current offset
            Retry,

            // proceed with the current token (possibly set by the handler)
            AcceptState,
        };
        virtual LexerAction invalidCharacter(Lexer*, char) = 0;
        virtual LexerAction unterminatedBlockComment(Lexer*, int_t beginOffset) = 0;
        virtual LexerAction unterminatedCharacterLiteral(Lexer*, int_t beginOffset, int_t endOffset) = 0;
        virtual LexerAction invalidCharacterLiteral(Lexer*, int_t beginOffset, int_t endOffset) = 0;
    };
    struct Instrumenter {
        virtual void handleComment(Lexer*) { }
        virtual void nextToken(Lexer*) { }
    };

    ErrorHandler* errorHandler = nullptr;
    Instrumenter* instrumenter = nullptr;

    WordStringTable wordTable { words };

    Lexer()
        : LexerState(std::string_view()) { }

    void setSource(std::string_view);

    TokenWithData fullToken() const { return *this; }
    SingleTokenSourceRange tokRange() const;
    WordAndLocation tokWord() const { return { std::get<Word>(tokData), tokRange() }; }
    NumericLiteral tokInt() const { return std::get<NumericLiteral>(tokData); }
    CharacterLiteral tokChar() const { return std::get<CharacterLiteral>(tokData); }
    std::string_view tokCommentSource() const;

    bool valid() const;

    std::string_view source(int_t begin, int_t end) const;
    SourcePosition sourcePosition(LocalSourceLocation) const;
    void formatLine(std::ostream&, LocalSourceRange highlight) const;

    // advance while skipping over comment and newline tokens
    void nextToken();
    void reemitLastToken(TokenWithData);
};

struct Parser : Lexer {
    struct Instrumenter {
        virtual void emitNode(Parser*, Node*) { }
        virtual void emitDecl(Parser*, Decl*) { }
    };
    struct ErrorHandler {
        virtual void expectedParameterName(Parser*) = 0;
        virtual void parameterModifierNotAllowed(Parser*, WordAndLocation modifier, WordAndLocation name) = 0;
        virtual void invalidParameterModifier(Parser*, WordAndLocation modifier, WordAndLocation name) = 0;
        virtual void unexpectedAfterParameter(Parser*, WordAndLocation name) = 0;

        virtual void expectedSemiColon(Parser*) = 0;

        virtual void expectedFunctionBody(Parser*) = 0;

        virtual void expectedIfBody(Parser*, bool statement) = 0;
        virtual void expectedElseBody(Parser*) = 0;

        virtual void expectedAccessExpr(Parser*) = 0;

        virtual void expectedExpression(Parser*) = 0;

        virtual void unexpectedAfterArgument(Parser*) = 0;
    };

    Instrumenter* instrumenter = nullptr;
    ErrorHandler* errorHandler = nullptr;

    StreamAllocator<4> nodeStream;
    Node* nextNodeLocation() const {
        return (Node*)(nodeStream.position());
    }
    template<std::derived_from<Node> T>
    T* emitNode(T in);

    StaticDeclContext* staticDeclContext = nullptr;
    ParameterDeclContext* parameterDeclContext = nullptr;
    template<std::derived_from<Decl> T, typename... Args>
    T* emitDeclInternal(Args&&...);
    template<std::derived_from<Decl> T, typename... Args>
    T* emitDecl(Args&&...);
    struct ParameterDeclContextHelper;

    NamespaceDecl* parseModule();
    void parseDeclaration();
    void parseNamespaceDecl();
    void parseTypeDecl(ParameterDeclContextHelper);
    void parseFunctionDecl(ParameterDeclContextHelper);
    void parseVariableDecl(WordAndLocation name, std::span<const WordAndLocation>, ParameterDeclContextHelper);
    void parseHasMemberDecl();
    enum class ParameterParseOptions {
        None,
        OnlyInParameters,
    };
    int_t parseParameters(ParameterParseOptions = ParameterParseOptions::None);
    void parseBodyExprOrStmt();

    void parseSingleOrCompoundStmt();
    void parseCompoundStmt();
    enum class ParsedStatementKind {
        Normal,
        ExprStmtWithMissingSemiColon,
    };
    void parseStatement();
    [[nodiscard]] ParsedStatementKind parseStatementInternal();
    void parseIfExprOrStmt(bool statement);

    void parseExpression();
    void parseCommaElseExpr();
    void parseCommaElseExprHere();
    void parseIfExpr();
    void parseBinaryOperatorExpr();
    void parseUnaryOperatorExpr();
    void parsePostfixExpr();
    void parsePostfixExprHere();
    void parsePrimaryExpr();
    void parseArguments();
};

template<std::derived_from<Node> T>
T* Parser::emitNode(T in) {
    T* node = std::construct_at(nodeStream.template allocate<T>(), in);

    if (instrumenter)
        instrumenter->emitNode(this, node);
    return node;
}