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
};

struct Parser : LexerState {
    struct ErrorHandler {
        enum class LexerAction {
            // try to lex a new token at the current offset
            Retry,

            // proceed with the current token (possibly set by the handler)
            AcceptState,
        };
        virtual LexerAction invalidCharacter(Parser*, char) = 0;
        virtual LexerAction unterminatedBlockComment(Parser*, int_t beginOffset) = 0;
        virtual LexerAction unterminatedCharacterLiteral(Parser*, int_t beginOffset, int_t endOffset) = 0;
        virtual LexerAction invalidCharacterLiteral(Parser*, int_t beginOffset, int_t endOffset) = 0;
    };
    struct Instrumenter {
        virtual void handleComment(Parser*) { }
        virtual void nextToken(Parser*) { }
        virtual void emitNode(Parser*, Node*) { }
        virtual void emitDecl(Parser*, Decl*) { }
    };

    ErrorHandler* errorHandler = nullptr;
    Instrumenter* instrumenter = nullptr;

    // advance while skipping over comment and newline tokens
    void nextToken();
    void reemitLastToken(TokenWithData);

    static constexpr auto words = ConstWordStringTable(
        keyword("if"), keyword("else"), keyword("namespace"), keyword("struct"), keyword("object"), keyword("fn"),
        keyword("with"), keyword("template"), keyword("mut"), keyword("let"), keyword("inout"), keyword("out"),
        keyword("static"));
    WordStringTable wordTable { words };

    StreamAllocator<4> nodeStream;
    Node* nextNodeLocation() const {
        return (Node*)(nodeStream.position());
    }
    template<std::derived_from<Node> T>
    T* emitNode(T in);

    struct DeclStackItem {
        Word name;
        // offset from the beginning of the stream
        node_stream_offset nodeStreamOffset;
    };
    HomogeneousStreamAllocator<DeclStackItem> staticDeclStack;
    HomogeneousStreamAllocator<DeclStackItem> parameterDeclStack;
    struct DeclarationScope;
    struct TemplatedDeclarationScope;
    template<std::derived_from<Decl> T, typename... Args>
    T* emitDecl(Args&&...);

    StaticDecl* parseModule();
    void parseDeclaration();
    void parseNamespaceOrTypeDecl(std::span<const WordAndLocation>, TemplatedDeclarationScope);
    void parseVariableDecl(WordAndLocation name, std::span<const WordAndLocation>, TemplatedDeclarationScope);
    void parseFunctionDecl(std::span<const WordAndLocation>, TemplatedDeclarationScope);
    enum class ParameterParseOptions {
        None,
        OnlyLetParameters,
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