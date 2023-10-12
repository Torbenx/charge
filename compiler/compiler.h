#pragma once

#include "WordTable.h"
#include "expr.h"
#include <new>

struct BumpAllocatorFields {
    std::byte* storage = nullptr;
    uint32_t alignedOffset = 0;
    uint32_t alignedCapacity = 0;
};
template<size_t defaultAlignment>
struct BumpAllocator : BumpAllocatorFields {
    static constexpr size_t MAX_ALIGNMENT = 16;
    static_assert(std::has_single_bit(defaultAlignment));

    BumpAllocator() { allocateStorage(1024 * 1024); }
    BumpAllocator(BumpAllocator&& other)
        : BumpAllocatorFields(other) {
        (BumpAllocatorFields&)other = {};
    }
    BumpAllocator& operator=(BumpAllocator&& other) {
        freeStorage();
        (BumpAllocatorFields&)* this = other;
        (BumpAllocatorFields&)other = {};
        return *this;
    }
    ~BumpAllocator() { freeStorage(); }

    template<typename T>
    T* allocate() {
        return (T*)allocate(alignof(T), sizeof(T));
    }
    void* allocate(int_t alignment, int_t size) {
        VERIFY(size > 0 && alignment > 0 && (size_t)alignment <= MAX_ALIGNMENT && std::has_single_bit((size_t)alignment));
        size_t alignedBegin = alignmentCeil(alignedOffset, aligned(alignment));
        size_t alignedEnd = alignedBegin + aligned(size);
        VERIFY(alignedEnd <= alignedCapacity);
        alignedOffset = alignedEnd;
        return storage + alignedBegin * defaultAlignment;
    }
    void* position() const {
        return storage + alignedOffset * defaultAlignment;
    }

private:
    void freeStorage() {
        if (storage == nullptr)
            return;
        operator delete(storage, alignedCapacity* defaultAlignment, std::align_val_t(MAX_ALIGNMENT));
        (BumpAllocatorFields&)* this = {};
    }
    void allocateStorage(size_t sizeInBytes) {
        freeStorage();
        alignedOffset = 0;
        alignedCapacity = aligned(sizeInBytes);
        storage = (std::byte*)operator new(alignedCapacity* defaultAlignment, std::align_val_t(defaultAlignment));
    }
    size_t aligned(size_t bytes) {
        return (bytes + defaultAlignment - 1) / defaultAlignment;
    }
};

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
struct TokenStream {
    static_assert(sizeof(StreamToken) == 8);
    BumpAllocator<8> allocator;
    void emit(StreamToken token) {
        auto prevOffset = allocator.alignedOffset;
        std::construct_at(allocator.allocate<StreamToken>(), token);
        VERIFY(allocator.alignedOffset = prevOffset + 1);
    }
    int_t size() const { return allocator.alignedOffset; }
    const StreamToken* data() const {
        return (StreamToken*)allocator.storage;
    }
    const StreamToken& operator[](int_t offset) const {
        return *(data() + offset);
    }
    const StreamToken* begin() const { return data(); }
    const StreamToken* end() const { return data() + size(); }
    const StreamToken& back() const { return (*this)[size() - 1]; }
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
    TokenStream tokenStream;

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
        keyword("if"), keyword("else"), keyword("namespace"), keyword("struct"), keyword("object"), keyword("fn"));
    WordStringTable wordTable { words };

    BumpAllocator<4> nodeAllocator;
    Node* nextNodeLocation() const {
        return (Node*)(nodeAllocator.position());
    }
    template<std::derived_from<Node> T>
    T* emitNode(T in);
    template<std::derived_from<Decl> T, typename... Args>
    T* emitDecl(Args&&...);

    id<Decl> parseDeclaration();
    id<Decl> parseNamespaceOrTypeDecl(std::span<const WordAndLocation>);
    id<Decl> parseVariableDecl(WordAndLocation name, std::span<const WordAndLocation>);
    id<Decl> parseFunctionDecl(std::span<const WordAndLocation>);
    enum class ParameterParseScope {
        Function,
        Template,
    };
    void parseParameters(ParameterParseScope);

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
    void parseBinaryOperatorExpr(int ambientPrecedence = 100);
    void parseUnaryOperatorExpr();
    void parsePostfixExpr();
    void parsePostfixExprHere();
    void parsePrimaryExpr();
    void parseArguments();
};