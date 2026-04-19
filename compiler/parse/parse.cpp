#include <WordTable.h>
#include <parse/Parser.h>
#include <parse/parse_gen.h>
#include <utility>

#ifdef __GNUC__
#define LABEL_MAYBE_UNUSED [[maybe_unused]]
#define NO_INLINE [[gnu::noinline]]
#else
#define LABEL_MAYBE_UNUSED
#define NO_INLINE
#endif

using namespace std::string_view_literals;
enum class DeclarationKind : uint8_t {
    Namespace,
    Struct,
    StaticVariable,
    Function,
    Member,
    BaseMember,
    Enum,
    EnumValue,
};

namespace parse {

LexerToken lexerToken(TokenKind semToken) {
#define TOKEN(kind, lexToken, data1, data) \
    case TokenKind::kind:                  \
        return LexerToken::lexToken;

    switch (semToken) {
#include <parse/tokens.inc>

    default:
        VERIFY_NOT_REACHED();
    }
}
static void checkLexToken(TokenKind semToken, LexerToken lexToken) {
    auto expected = lexerToken(semToken);
    VERIFY(expected == LexerToken::Invalid || lexToken == expected);
}
static void checkTokenUpdate(TokenKind oldKind, TokenKind newKind) {
    VERIFY(lexerToken(oldKind) == lexerToken(newKind));
}

static constexpr bool isWordBulkCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

static bool isWordFirstCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || c == '_' || c == '$' || c == '#';
}

static void setData1(TokenInfo& token, uint32_t data1Bits) {
    token.data1Bits = data1Bits;
}
static void setData2(TokenInfo& token, uint32_t data2Bits) {
    token.data2Bits = data2Bits;
}

static void setData1(SimpleTokenInfo&, uint32_t) { }
static void setData2(SimpleTokenInfo&, uint32_t) { }

static ScopeKind* pushScope(ScopeKind* position, ScopeKind kind) {
    // println("pushScope {}", nameString(kind));
    auto index = ScopeBuffer::toIndex(position);
    VERIFY(index + 1 < (size_t)SCOPE_BUFFER_SIZE);
    position += 1;
    position[0] = kind;
    return position;
}

template<typename... Args>
static ScopeKind* popScope(ScopeKind* position, Args... kinds) {
    // println("popScope {}", nameString(*position));
    static_assert((std::is_same_v<Args, ScopeKind> && ...));
    if (((position[0] != kinds) && ...))
        return nullptr;
    position -= 1;
    return position;
}

static Word* addCallArgument(Word* position, Word name, sema::Context&) {
    auto index = ArgumentBuffer::toIndex(position);
    VERIFY(index + 1 < (size_t)ARGUMENT_BUFFER_SIZE);
    uint32_t newCount = position[0].toUint() + 1;
    position[0] = name;
    position += 1;
    position[0] = Word::fromUint(newCount);
    return position;
}

static void updateCallArgument(Word* position, Word name, sema::Context&) {
    uint32_t count = position[0].toUint();
    VERIFY(count != 0);
    position[-1] = name;
}

NO_INLINE static Word* endCall(Word* position, sema::Context& output) {
    uint32_t count = position[0].toUint();
    auto handle = output.tokenBuffer.addCallArguments({ position - count, position });
    position -= count + 2;
    output.tokenBuffer.tokens[position[1].toUint()].setData1<DataKind::CallArguments>(handle);
    return position;
}

static Word* addCallArgument(Word* ptr, Word, SimpleOutput&) { return ptr; }

static void updateCallArgument(Word*, Word, SimpleOutput&) { }

static Word* endCall(Word* ptr, SimpleOutput&) { return ptr; }

static SourceLocation locationInCurrentLine(const char* position, sema::Context& output) {
    return {
        0u,
        (uint32_t)output.tokenBuffer.lines.size() - 1,
        (uint32_t)(position - output.tokenBuffer.lines.back().begin)
    };
}

NO_INLINE static void emitToken(TokenKind kind, const char* begin, uint32_t data, sema::Context& output) {
    output.tokenBuffer.tokens.push_back({ kind, locationInCurrentLine(begin, output), data });
}

NO_INLINE static void discardLastToken(sema::Context& output) {
    output.tokenBuffer.tokens.pop_back();
}

NO_INLINE static void emitToken(TokenKind kind, const char*, uint32_t, SimpleOutput& output) {
    output.tokenBuffer.tokens.push_back(kind);
}

NO_INLINE static void discardLastToken(SimpleOutput& output) {
    if (!output.tokenBuffer.tokens.empty())
        output.tokenBuffer.tokens.pop_back();
}

NO_INLINE static Word* emitCallToken(Word* argPos, TokenKind kind, const char* begin, sema::Context& output) {
    uint32_t tokenIndex = output.tokenBuffer.tokens.size();
    output.tokenBuffer.tokens.push_back({ kind, locationInCurrentLine(begin, output), 0 });

    auto index = ArgumentBuffer::toIndex(argPos);
    VERIFY(index + 2 < (size_t)ARGUMENT_BUFFER_SIZE);
    argPos[1] = Word::fromUint(tokenIndex);
    argPos[2] = Word::fromUint(0);
    argPos += 2;
    return argPos;
}
NO_INLINE static Word* emitCallToken(Word* ptr, TokenKind kind, const char*, SimpleOutput& output) {
    output.tokenBuffer.tokens.push_back(kind);
    return ptr;
}

NO_INLINE static void markLineBegin(const char* position, sema::Context& output) {
    output.tokenBuffer.lines.push_back({ position });
}
static void markLineBegin(const char*, SimpleOutput&) { }

struct WordAndPosition {
    const char* position;
    Word word;
};
[[nodiscard]] NO_INLINE static WordAndPosition readWord(const char* position, sema::Context& output) {
    const char* wordBegin = position;
    Word::HashState hashState;
    do {
        Word::iterateHash(hashState, position[0]);
        position += 1;
    } while (isWordBulkCharacter(position[0]));
    auto hash = Word::finalizeHash(hashState);
    Word word = output.tokenBuffer.wordTable.getWithHash(std::string_view(wordBegin, position), hash);
    return { position, word };
}
[[nodiscard]] NO_INLINE static WordAndPosition readWord(const char* position, SimpleOutput&) {
    const char* wordBegin = position;
    Word::HashState hashState;
    do {
        Word::iterateHash(hashState, position[0]);
        position += 1;
    } while (isWordBulkCharacter(position[0]));
    auto hash = Word::finalizeHash(hashState);
    Word word = words.findWithHash(std::string_view(wordBegin, position), hash);
    return { position, word.empty() ? words["T"] : word };
}

[[nodiscard]] static const char* skipWhitespace(const char* position) {
    while (position[0] == ' ' || position[0] == '\t')
        position += 1;
    return position;
}

// advances offset to the next '*/'
[[nodiscard]] static const char* skipToEndOfBlockComment(const char* position) {
    while (position[0] != '\0' && !(position[0] == '*' && position[1] == '/')) {
        position += 1;
    }
    return position;
};

// advances offset to the next new line character
[[nodiscard]] static const char* skipToEndOfLine(const char* position) {
    while (position[0] != '\0' && position[0] != '\n' && position[0] != '\r') {
        position += 1;
    }
    return position;
};

[[nodiscard]] static const char* skipToEndOfCharacterLiteral(const char* position) {
    position += 1;
    while (position[0] != '\0' && position[0] != '\'' && position[0] != '\n' && position[0] != '\r') {
        position += 1;
    }
    return position;
};

NO_INLINE static void emitWhitespace(WhitespaceKind kind, const char* begin, const char* end, sema::Context& output) {
    output.tokenBuffer.whitespace.push_back({ { kind, locationInCurrentLine(begin, output) }, (uint32_t)(end - begin) });
}
static void emitWhitespace(WhitespaceKind, const char*, const char*, SimpleOutput&) { }

template<typename ParseOutput>
[[nodiscard]] NO_INLINE static const char* inlineAdvancer(const char* tokEnd, ParseOutput& output) {
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        const char* tokBegin = tokEnd;
        if (std::string_view(tokEnd, 2) == "//") {
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            continue;
        }
        if (std::string_view(tokEnd, 2) == "/*") {
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            continue;
        }
        if (tokEnd[0] == '\n') {
            tokEnd += 1;
            markLineBegin(tokEnd, output);
            continue;
        }
        if (tokEnd[0] == '\r') {
            if (tokEnd[1] == '\n') {
                tokEnd += 2;
            } else {
                tokEnd += 1;
            }
            markLineBegin(tokEnd, output);
            continue;
        }
        break;
    }
    return tokEnd;
}

static sema::ProgramKind programKindForDeclaration(DeclarationKind kind) {
    switch (kind) {
    case DeclarationKind::Struct:
        return sema::ProgramKind::Struct;
    case DeclarationKind::Function:
        return sema::ProgramKind::Function;
    case DeclarationKind::StaticVariable:
        return sema::ProgramKind::Global;
    case DeclarationKind::Enum:
        return sema::ProgramKind::Enum;
    default:
        VERIFY_NOT_REACHED();
    }
}

template<DeclarationKind kind>
static sema::DeclarationValue commitDeclaration(Word name, const char* currentPosition, TokenHandle declarationBegin, sema::Context& output) {
    // println("commitDeclaration {}", output.tokenBuffer.wordTable.view(name));
    if constexpr (kind == DeclarationKind::Member || kind == DeclarationKind::BaseMember) {
        return output.pushMemberScope(kind == DeclarationKind::BaseMember, name, declarationBegin, locationInCurrentLine(currentPosition, output));
    } else if constexpr (kind == DeclarationKind::EnumValue) {
        return output.pushEnumValueScope(name, declarationBegin, locationInCurrentLine(currentPosition, output));
    } else if constexpr (kind == DeclarationKind::Namespace) {
        return output.pushNamespaceScope(name);
    } else {
        return output.pushStaticScope(programKindForDeclaration(kind), name, declarationBegin, locationInCurrentLine(currentPosition, output));
    }
}

template<DeclarationKind kind>
static sema::DeclarationValue commitImplDeclaration(const char* currentPosition, TokenHandle declarationBegin, sema::Context& output) {
    static_assert(kind == DeclarationKind::Struct || kind == DeclarationKind::Function || kind == DeclarationKind::Enum || kind == DeclarationKind::StaticVariable);
    return output.pushStaticImplScope(programKindForDeclaration(kind), declarationBegin, locationInCurrentLine(currentPosition, output));
}

static void endDeclaration(sema::Context& output) {
    // println("endDeclaration {}", output.tokenBuffer.wordTable.view(output.currentScope()->name()));
    output.popScope(output.tokenBuffer.currentToken());
}

using GlobalKind = sema::GlobalKind;
static void setGlobalKind(sema::Context& output, GlobalKind kind) {
    sema::cast<sema::GlobalProgram>(output.currentProgram())->m_globalKind = kind;
}

template<DeclarationKind kind>
static sema::DeclarationValue commitDeclaration(Word, const char*, TokenHandle, SimpleOutput&) {
    return sema::INVALID_DECLARATION_VALUE;
}

template<DeclarationKind kind>
static sema::DeclarationValue commitImplDeclaration(const char*, TokenHandle, SimpleOutput&) {
    return sema::INVALID_DECLARATION_VALUE;
}

static void endDeclaration(SimpleOutput&) { }

using GlobalKind = sema::GlobalKind;
static void setGlobalKind(SimpleOutput&, GlobalKind) { }

template<typename ParseOutput>
LexerToken lexImpl(char const*& tokEnd, ParseOutput& output) {
    const char* tokBegin = tokEnd;
    Word this_identifier;

lex$no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, output);
        goto lex$no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, output);
        goto lex$no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::ExclaimEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Exclaim;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::PercentEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Percent;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // lexToken
                return LexerToken::AmpAmpEqual;
            }
            tokEnd += 2;
            // lexToken
            return LexerToken::AmpAmp;
        }
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::AmpEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Amp;
    }
    case '(': {
        tokEnd += 1;
        // lexToken
        return LexerToken::LeftParen;
    }
    case ')': {
        tokEnd += 1;
        // lexToken
        return LexerToken::RightParen;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::StarEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Star;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // lexToken
            return LexerToken::PlusPlus;
        }
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::PlusEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Plus;
    }
    case ',': {
        tokEnd += 1;
        // lexToken
        return LexerToken::Comma;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // lexToken
            return LexerToken::MinusMinus;
        }
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::MinusEqual;
        }
        if (next == '>') {
            tokEnd += 2;
            // lexToken
            return LexerToken::MinusGreater;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Minus;
    }
    case '.': {
        tokEnd += 1;
        // lexToken
        return LexerToken::Point;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            goto lex$no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            goto lex$no_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::SlashEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Slash;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // lexToken
            return LexerToken::ColonColon;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Colon;
    }
    case ';': {
        tokEnd += 1;
        // lexToken
        return LexerToken::SemiColon;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // lexToken
                return LexerToken::LessLessEqual;
            }
            tokEnd += 2;
            // lexToken
            return LexerToken::LessLess;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // lexToken
                return LexerToken::LessEqualGreater;
            }
            tokEnd += 2;
            // lexToken
            return LexerToken::LessEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Less;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::EqualEqual;
        }
        if (next == '>') {
            tokEnd += 2;
            // lexToken
            return LexerToken::EqualGreater;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Equal;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::GreaterEqual;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // lexToken
                return LexerToken::GreaterGreaterEqual;
            }
            tokEnd += 2;
            // lexToken
            return LexerToken::GreaterGreater;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Greater;
    }
    case '[': {
        tokEnd += 1;
        // lexToken
        return LexerToken::LeftSquare;
    }
    case ']': {
        tokEnd += 1;
        // lexToken
        return LexerToken::RightSquare;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::HatEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Hat;
    }
    case '{': {
        tokEnd += 1;
        // lexToken
        return LexerToken::LeftBrace;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::VertEqual;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // lexToken
                return LexerToken::VertVertEqual;
            }
            tokEnd += 2;
            // lexToken
            return LexerToken::VertVert;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Vert;
    }
    case '}': {
        tokEnd += 1;
        // lexToken
        return LexerToken::RightBrace;
    }
    case '~': {
        tokEnd += 1;
        // lexToken
        return LexerToken::Tilde;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        do {
            tokEnd += 1;
        } while (tokEnd[0] >= '0' && tokEnd[0] <= '9');
        // lexToken
        return LexerToken::Literal;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // lexToken
        return LexerToken::Literal;
    }
    case '\0':
        // lexToken
        return LexerToken::EOS;
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '#':
    case '$':
    case '_':
        goto lex$word_case_entry;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
lex$word_case_entry:
    {
        auto wordAndPos = readWord(tokEnd, output);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (isKeyword(this_identifier)) {
    LABEL_MAYBE_UNUSED lex$keyword_check:
        if (this_identifier == words["assert"]) {
            // lexToken
            return LexerToken::Assert;
        }
        if (this_identifier == words["break"]) {
            // lexToken
            return LexerToken::Break;
        }
        if (this_identifier == words["catch"]) {
            // lexToken
            return LexerToken::Catch;
        }
        if (this_identifier == words["const"]) {
            // lexToken
            return LexerToken::Const;
        }
        if (this_identifier == words["continue"]) {
            // lexToken
            return LexerToken::Continue;
        }
        if (this_identifier == words["destroy"]) {
            // lexToken
            return LexerToken::Destroy;
        }
        if (this_identifier == words["discard"]) {
            // lexToken
            return LexerToken::Discard;
        }
        if (this_identifier == words["do"]) {
            // lexToken
            return LexerToken::Do;
        }
        if (this_identifier == words["elif"]) {
            // lexToken
            return LexerToken::Elif;
        }
        if (this_identifier == words["else"]) {
            // lexToken
            return LexerToken::Else;
        }
        if (this_identifier == words["for"]) {
            // lexToken
            return LexerToken::For;
        }
        if (this_identifier == words["if"]) {
            // lexToken
            return LexerToken::If;
        }
        if (this_identifier == words["impl"]) {
            // lexToken
            return LexerToken::Impl;
        }
        if (this_identifier == words["let"]) {
            // lexToken
            return LexerToken::Let;
        }
        if (this_identifier == words["return"]) {
            // lexToken
            return LexerToken::Return;
        }
        if (this_identifier == words["shared"]) {
            // lexToken
            return LexerToken::Shared;
        }
        if (this_identifier == words["static"]) {
            // lexToken
            return LexerToken::Static;
        }
        if (this_identifier == words["try"]) {
            // lexToken
            return LexerToken::Try;
        }
        if (this_identifier == words["unique"]) {
            // lexToken
            return LexerToken::Unique;
        }
        if (this_identifier == words["var"]) {
            // lexToken
            return LexerToken::Var;
        }
        if (this_identifier == words["while"]) {
            // lexToken
            return LexerToken::While;
        }
        if (this_identifier == words["enum"]) {
            // lexToken
            return LexerToken::Enum;
        }
        if (this_identifier == words["fn"]) {
            // lexToken
            return LexerToken::Fn;
        }
        if (this_identifier == words["base"]) {
            // lexToken
            return LexerToken::Base;
        }
        if (this_identifier == words["incomplete"]) {
            // lexToken
            return LexerToken::Incomplete;
        }
        if (this_identifier == words["namespace"]) {
            // lexToken
            return LexerToken::Namespace;
        }
        if (this_identifier == words["open"]) {
            // lexToken
            return LexerToken::Open;
        }
        if (this_identifier == words["struct"]) {
            // lexToken
            return LexerToken::Struct;
        }
        if (this_identifier == words["template"]) {
            // lexToken
            return LexerToken::Template;
        }
        if (this_identifier == words["trait"]) {
            // lexToken
            return LexerToken::Trait;
        }
        if (this_identifier == words["virtual"]) {
            // lexToken
            return LexerToken::Virtual;
        }
        // -> error
        goto error$as_then;
    }
LABEL_MAYBE_UNUSED lex$identifier_case:
    if (isSpecialIdentifier(this_identifier)) {
    }
    // lexToken
    return LexerToken::Identifier;

error$as_then:
    VERIFY_NOT_REACHED();
}

template<typename ParseOutput>
Parser::InternalState Parser::parseImpl(const Parser::InternalState& inState, ParseOutput& output, int_t tokenLimit) {
    State parseState = inState.state;
    State continueState = inState.continueState;
    const char* tokBegin = inState.sourcePosition;
    const char* tokEnd = inState.sourcePosition;
    ScopeKind* scopePosition = inState.scopePosition;
    ScopeKind* savedScopePosition = inState.scopePosition;
    Word* argumentPosition = inState.argumentPosition;
    TokenKind carriedEmitTokenKind = (TokenKind)0;
    uint32_t carriedEmitTokenData = 0;
    Word this_identifier;
    sema::DeclarationValue this_declaration = sema::INVALID_DECLARATION_VALUE;
    TokenHandle declarationBegin = inState.declarationBegin;
    Word argumentName = inState.savedArgumentName;

    switch (continueState) {
    case State::Expression:
        goto expression$no_emit;
    case State::AfterExpression:
        goto after_expression$no_emit;
    case State::CommaAfterExpression:
        goto comma_after_expression$no_emit;
    case State::CommaElse:
        goto comma_else$no_emit;
    case State::Argument:
        goto argument$no_emit;
    case State::CheckDesignatedArgument:
        goto check_designated_argument$no_emit;
    case State::MaybeDesignatedArgument:
        goto maybe_designated_argument$no_emit;
    case State::FirstArgumentParen:
        goto first_argument_paren$no_emit;
    case State::FirstArgumentSquare:
        goto first_argument_square$no_emit;
    case State::FirstArgumentBrace:
        goto first_argument_brace$no_emit;
    case State::MemberAccess:
        goto member_access$no_emit;
    case State::StaticAccess:
        goto static_access$no_emit;
    case State::SingleOrCompoundStatement:
        goto single_or_compound_statement$no_emit;
    case State::AfterStatement:
        goto after_statement$no_emit;
    case State::Statement:
        goto statement$no_emit;
    case State::LetStatement:
        goto let_statement$no_emit;
    case State::VarStatement:
        goto var_statement$no_emit;
    case State::AfterReturn:
        goto after_return$no_emit;
    case State::ElseBranch:
        goto else_branch$no_emit;
    case State::AfterSimpleVariableDeclarationId:
        goto after_simple_variable_declaration_id$no_emit;
    case State::AfterVariableDeclarationId:
        goto after_variable_declaration_id$no_emit;
    case State::VariableType:
        goto variable_type$no_emit;
    case State::AfterVariableModifier:
        goto after_variable_modifier$no_emit;
    case State::AfterVariableUniqueModifier:
        goto after_variable_unique_modifier$no_emit;
    case State::AfterVariableSharedModifier:
        goto after_variable_shared_modifier$no_emit;
    case State::AfterVariableConstModifier:
        goto after_variable_const_modifier$no_emit;
    case State::AfterParameters:
        goto after_parameters$no_emit;
    case State::FirstParameter:
        goto first_parameter$no_emit;
    case State::Parameter:
        goto parameter$no_emit;
    case State::VarParameter:
        goto var_parameter$no_emit;
    case State::ImplExpression:
        goto impl_expression$no_emit;
    case State::AfterImplExpression:
        goto after_impl_expression$no_emit;
    case State::ImplAccessExpression:
        goto impl_access_expression$no_emit;
    case State::NoDeclaration:
        VERIFY_NOT_REACHED();
    case State::NamespaceDeclaration:
        goto namespace_declaration$no_emit;
    case State::NamespaceDeclarationId:
        goto namespace_declaration_id$no_emit;
    case State::AfterNamespaceDeclarationId:
        goto after_namespace_declaration_id$no_emit;
    case State::NamespaceDeclarationBody:
        goto namespace_declaration_body$no_emit;
    case State::TemplatedDeclaration:
        VERIFY_NOT_REACHED();
    case State::TemplatedDeclarationWithAttributes:
        goto templated_declaration_with_attributes$no_emit;
    case State::AfterTemplate:
        goto after_template$no_emit;
    case State::AfterTemplateParameters:
        goto after_template_parameters$no_emit;
    case State::FunctionDeclarationId:
        goto function_declaration_id$no_emit;
    case State::AfterFunctionDeclarationId:
        goto after_function_declaration_id$no_emit;
    case State::AfterFunctionParameters:
        goto after_function_parameters$no_emit;
    case State::StructDeclarationId:
        goto struct_declaration_id$no_emit;
    case State::AfterStructDeclarationId:
        goto after_struct_declaration_id$no_emit;
    case State::StructDeclarationBody:
        goto struct_declaration_body$no_emit;
    case State::MemberDeclaration:
        goto member_declaration$no_emit;
    case State::EnumDeclarationId:
        goto enum_declaration_id$no_emit;
    case State::AfterEnumDeclarationId:
        goto after_enum_declaration_id$no_emit;
    case State::EnumDeclarationBody:
        goto enum_declaration_body$no_emit;
    case State::EnumValueDeclaration:
        goto enum_value_declaration$no_emit;
    case State::AfterEnumValueDeclarationId:
        goto after_enum_value_declaration_id$no_emit;
    case State::AfterStatic:
        goto after_static$no_emit;
    case State::StaticVarVariableDeclaration:
        goto static_var_variable_declaration$no_emit;
    case State::StaticOpenVariableDeclaration:
        goto static_open_variable_declaration$no_emit;
    case State::AfterDeclaration:
        goto after_declaration$no_emit;
    case State::Error:
        VERIFY_NOT_REACHED();
    }
    // SwitchState expression
expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
expression$no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::Expression;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
expression$as_then:
    continueState = State::Expression;
    savedScopePosition = scopePosition;
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, output);
        goto expression$no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, output);
        goto expression$no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::LogicalNotExpr
        checkLexToken(TokenKind::LogicalNotExpr, LexerToken::Exclaim);
        carriedEmitTokenKind = TokenKind::LogicalNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '(': {
        tokEnd += 1;
        // emitCallToken TokenKind::ParenthesizedExpr
        argumentPosition = emitCallToken(argumentPosition, TokenKind::ParenthesizedExpr, tokBegin, output);
        // next first_argument_paren
        goto first_argument_paren$no_emit;
    }
    case ')': {
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::DereferenceExpr
        checkLexToken(TokenKind::DereferenceExpr, LexerToken::Star);
        carriedEmitTokenKind = TokenKind::DereferenceExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitToken TokenKind::PreIncrementExpr
            checkLexToken(TokenKind::PreIncrementExpr, LexerToken::PlusPlus);
            carriedEmitTokenKind = TokenKind::PreIncrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::PlusExpr
        checkLexToken(TokenKind::PlusExpr, LexerToken::Plus);
        carriedEmitTokenKind = TokenKind::PlusExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ',': {
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // emitToken TokenKind::PreDecrementExpr
            checkLexToken(TokenKind::PreDecrementExpr, LexerToken::MinusMinus);
            carriedEmitTokenKind = TokenKind::PreDecrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::NegateExpr
        checkLexToken(TokenKind::NegateExpr, LexerToken::Minus);
        carriedEmitTokenKind = TokenKind::NegateExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // emitToken TokenKind::ImplicitSelfReference
        checkLexToken(TokenKind::ImplicitSelfReference, LexerToken::Point);
        carriedEmitTokenKind = TokenKind::ImplicitSelfReference;
        carriedEmitTokenData = 0;
        // next member_access
        goto member_access$with_emit;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            goto expression$no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            goto expression$no_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case ';': {
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '[': {
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case ']': {
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '{': {
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '}': {
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '~': {
        tokEnd += 1;
        // emitToken TokenKind::BitwiseNotExpr
        checkLexToken(TokenKind::BitwiseNotExpr, LexerToken::Tilde);
        carriedEmitTokenKind = TokenKind::BitwiseNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        do {
            tokEnd += 1;
        } while (tokEnd[0] >= '0' && tokEnd[0] <= '9');
        // emitToken TokenKind::LiteralExpr
        checkLexToken(TokenKind::LiteralExpr, LexerToken::Literal);
        carriedEmitTokenKind = TokenKind::LiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // emitToken TokenKind::LiteralExpr
        checkLexToken(TokenKind::LiteralExpr, LexerToken::Literal);
        carriedEmitTokenKind = TokenKind::LiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '\0':
        // -> error
        goto error$as_then;
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '#':
    case '$':
    case '_':
        goto expression$word_case_entry;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
expression$word_case_entry:
    {
        auto wordAndPos = readWord(tokEnd, output);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (isKeyword(this_identifier)) {
    LABEL_MAYBE_UNUSED expression$keyword_check:
        if (this_identifier == words["if"]) {
            // pushScope ScopeKind::IfExpr
            scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
            // next expression
            goto expression$no_emit;
        }
        // -> error
        goto error$as_then;
    }
LABEL_MAYBE_UNUSED expression$identifier_case:
    if (isSpecialIdentifier(this_identifier)) {
    }
    // emitToken TokenKind::IdentifierExpr
    checkLexToken(TokenKind::IdentifierExpr, LexerToken::Identifier);
    carriedEmitTokenKind = TokenKind::IdentifierExpr;
    carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
    // next after_expression
    goto after_expression$with_emit;

    // SwitchState after_expression
after_expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_expression$no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::AfterExpression;
    continueState = State::AfterExpression;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_expression$as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, output);
        goto after_expression$no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, output);
        goto after_expression$no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareNotEqualExpr
            checkLexToken(TokenKind::CompareNotEqualExpr, LexerToken::ExclaimEqual);
            carriedEmitTokenKind = TokenKind::CompareNotEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::RemainderUpdateStmt
            checkLexToken(TokenKind::RemainderUpdateStmt, LexerToken::PercentEqual);
            carriedEmitTokenKind = TokenKind::RemainderUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::RemainderExpr
        checkLexToken(TokenKind::RemainderExpr, LexerToken::Percent);
        carriedEmitTokenKind = TokenKind::RemainderExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // popScope ScopeKind::LeftExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::LogicalAndUpdateStmt
                checkLexToken(TokenKind::LogicalAndUpdateStmt, LexerToken::AmpAmpEqual);
                carriedEmitTokenKind = TokenKind::LogicalAndUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::LogicalAndExpr
            checkLexToken(TokenKind::LogicalAndExpr, LexerToken::AmpAmp);
            carriedEmitTokenKind = TokenKind::LogicalAndExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::BitwiseAndUpdateStmt
            checkLexToken(TokenKind::BitwiseAndUpdateStmt, LexerToken::AmpEqual);
            carriedEmitTokenKind = TokenKind::BitwiseAndUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseAndExpr
        checkLexToken(TokenKind::BitwiseAndExpr, LexerToken::Amp);
        carriedEmitTokenKind = TokenKind::BitwiseAndExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '(': {
        tokEnd += 1;
        // emitCallToken TokenKind::CallExpr
        argumentPosition = emitCallToken(argumentPosition, TokenKind::CallExpr, tokBegin, output);
        // next first_argument_paren
        goto first_argument_paren$no_emit;
    }
    case ')': {
        tokEnd += 1;
        // ifScope ScopeKind::RightExpr
        if (scopePosition[0] == ScopeKind::RightExpr) {
            // popScope ScopeKind::RightExpr
            {
                auto result = popScope(scopePosition, ScopeKind::RightExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::ExpressionStmt
            checkLexToken(TokenKind::ExpressionStmt, LexerToken::RightParen);
            carriedEmitTokenKind = TokenKind::ExpressionStmt;
            carriedEmitTokenData = 0;
            // next after_parameters
            goto after_parameters$with_emit;
        }
        // ifScope ScopeKind::VariableType
        if (scopePosition[0] == ScopeKind::VariableType) {
            // popScope ScopeKind::VariableType
            {
                auto result = popScope(scopePosition, ScopeKind::VariableType);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::ExpressionStmt
            checkLexToken(TokenKind::ExpressionStmt, LexerToken::RightParen);
            carriedEmitTokenKind = TokenKind::ExpressionStmt;
            carriedEmitTokenData = 0;
            // next after_parameters
            goto after_parameters$with_emit;
        }
        // ifScope ScopeKind::ParenInImplExpr
        if (scopePosition[0] == ScopeKind::ParenInImplExpr) {
            // popScope ScopeKind::ParenInImplExpr
            {
                auto result = popScope(scopePosition, ScopeKind::ParenInImplExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::EmptyNode
            checkLexToken(TokenKind::EmptyNode, LexerToken::RightParen);
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next after_impl_expression
            goto after_impl_expression$with_emit;
        }
        // popScope ScopeKind::Paren
        {
            auto result = popScope(scopePosition, ScopeKind::Paren);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightParen);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::MultiplyUpdateStmt
            checkLexToken(TokenKind::MultiplyUpdateStmt, LexerToken::StarEqual);
            carriedEmitTokenKind = TokenKind::MultiplyUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::MultiplyExpr
        checkLexToken(TokenKind::MultiplyExpr, LexerToken::Star);
        carriedEmitTokenKind = TokenKind::MultiplyExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // emitToken TokenKind::PostIncrementExpr
            checkLexToken(TokenKind::PostIncrementExpr, LexerToken::PlusPlus);
            carriedEmitTokenKind = TokenKind::PostIncrementExpr;
            carriedEmitTokenData = 0;
            // next after_expression
            goto after_expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AdditionUpdateStmt
            checkLexToken(TokenKind::AdditionUpdateStmt, LexerToken::PlusEqual);
            carriedEmitTokenKind = TokenKind::AdditionUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::AdditionExpr
        checkLexToken(TokenKind::AdditionExpr, LexerToken::Plus);
        carriedEmitTokenKind = TokenKind::AdditionExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ',': {
        tokEnd += 1;
        // next comma_after_expression
        goto comma_after_expression$no_emit;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // emitToken TokenKind::PostDecrementExpr
            checkLexToken(TokenKind::PostDecrementExpr, LexerToken::MinusMinus);
            carriedEmitTokenKind = TokenKind::PostDecrementExpr;
            carriedEmitTokenData = 0;
            // next after_expression
            goto after_expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::SubtractionUpdateStmt
            checkLexToken(TokenKind::SubtractionUpdateStmt, LexerToken::MinusEqual);
            carriedEmitTokenKind = TokenKind::SubtractionUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '>') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::SubtractionExpr
        checkLexToken(TokenKind::SubtractionExpr, LexerToken::Minus);
        carriedEmitTokenKind = TokenKind::SubtractionExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // next member_access
        goto member_access$no_emit;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            goto after_expression$no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            goto after_expression$no_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::DivideUpdateStmt
            checkLexToken(TokenKind::DivideUpdateStmt, LexerToken::SlashEqual);
            carriedEmitTokenKind = TokenKind::DivideUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::DivideExpr
        checkLexToken(TokenKind::DivideExpr, LexerToken::Slash);
        carriedEmitTokenKind = TokenKind::DivideExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // next static_access
            goto static_access$no_emit;
        }
        tokEnd += 1;
        // ifScope ScopeKind::BaseTypeExpr
        if (scopePosition[0] == ScopeKind::BaseTypeExpr) {
            // popScope ScopeKind::BaseTypeExpr
            {
                auto result = popScope(scopePosition, ScopeKind::BaseTypeExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // next struct_declaration_body
            goto struct_declaration_body$no_emit;
        }
        // ifScope ScopeKind::ReturnType
        if (scopePosition[0] == ScopeKind::ReturnType) {
            // popScope ScopeKind::ReturnType
            {
                auto result = popScope(scopePosition, ScopeKind::ReturnType);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::FunctionBody
            scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
            // emitToken TokenKind::FunctionBody
            checkLexToken(TokenKind::FunctionBody, LexerToken::Colon);
            carriedEmitTokenKind = TokenKind::FunctionBody;
            carriedEmitTokenData = 0;
            // next single_or_compound_statement
            goto single_or_compound_statement$with_emit;
        }
        // popScope ScopeKind::IfExprOrStmt
        {
            auto result = popScope(scopePosition, ScopeKind::IfExprOrStmt);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::LeftExpr
        {
            auto result = popScope(scopePosition, ScopeKind::LeftExpr);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::IfBranch
        scopePosition = pushScope(scopePosition, ScopeKind::IfBranch);
        // emitToken TokenKind::IfStmt
        checkLexToken(TokenKind::IfStmt, LexerToken::Colon);
        carriedEmitTokenKind = TokenKind::IfStmt;
        carriedEmitTokenData = 0;
        // next single_or_compound_statement
        goto single_or_compound_statement$with_emit;
    }
    case ';': {
        tokEnd += 1;
        // ifScope ScopeKind::BaseTypeExpr
        if (scopePosition[0] == ScopeKind::BaseTypeExpr) {
            // popScope ScopeKind::BaseTypeExpr
            {
                auto result = popScope(scopePosition, ScopeKind::BaseTypeExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::EmptyNode
            checkLexToken(TokenKind::EmptyNode, LexerToken::SemiColon);
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next after_declaration
            goto after_declaration$with_emit;
        }
        // popScope ScopeKind::LeftExpr, ScopeKind::RightExpr, ScopeKind::VariableType
        {
            auto result = popScope(scopePosition, ScopeKind::LeftExpr, ScopeKind::RightExpr, ScopeKind::VariableType);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::SemiColon);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_statement
        goto after_statement$with_emit;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // popScope ScopeKind::LeftExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::ShiftLeftUpdateStmt
                checkLexToken(TokenKind::ShiftLeftUpdateStmt, LexerToken::LessLessEqual);
                carriedEmitTokenKind = TokenKind::ShiftLeftUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::ShiftLeftExpr
            checkLexToken(TokenKind::ShiftLeftExpr, LexerToken::LessLess);
            carriedEmitTokenKind = TokenKind::ShiftLeftExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // emitToken TokenKind::CompareLessEqualExpr
            checkLexToken(TokenKind::CompareLessEqualExpr, LexerToken::LessEqual);
            carriedEmitTokenKind = TokenKind::CompareLessEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::CompareLessExpr
        checkLexToken(TokenKind::CompareLessExpr, LexerToken::Less);
        carriedEmitTokenKind = TokenKind::CompareLessExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareEqualExpr
            checkLexToken(TokenKind::CompareEqualExpr, LexerToken::EqualEqual);
            carriedEmitTokenKind = TokenKind::CompareEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '>') {
            tokEnd += 2;
            // popScope ScopeKind::IfExpr, ScopeKind::IfExprOrStmt
            {
                auto result = popScope(scopePosition, ScopeKind::IfExpr, ScopeKind::IfExprOrStmt);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::IfExpr
            checkLexToken(TokenKind::IfExpr, LexerToken::EqualGreater);
            carriedEmitTokenKind = TokenKind::IfExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // popScope ScopeKind::VariableType, ScopeKind::LeftExpr
        {
            auto result = popScope(scopePosition, ScopeKind::VariableType, ScopeKind::LeftExpr);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // emitToken TokenKind::AssignStmt
        checkLexToken(TokenKind::AssignStmt, LexerToken::Equal);
        carriedEmitTokenKind = TokenKind::AssignStmt;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareGreaterEqualExpr
            checkLexToken(TokenKind::CompareGreaterEqualExpr, LexerToken::GreaterEqual);
            carriedEmitTokenKind = TokenKind::CompareGreaterEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // popScope ScopeKind::LeftExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::ShiftRightUpdateStmt
                checkLexToken(TokenKind::ShiftRightUpdateStmt, LexerToken::GreaterGreaterEqual);
                carriedEmitTokenKind = TokenKind::ShiftRightUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::ShiftRightExpr
            checkLexToken(TokenKind::ShiftRightExpr, LexerToken::GreaterGreater);
            carriedEmitTokenKind = TokenKind::ShiftRightExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::CompareGreaterExpr
        checkLexToken(TokenKind::CompareGreaterExpr, LexerToken::Greater);
        carriedEmitTokenKind = TokenKind::CompareGreaterExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '[': {
        tokEnd += 1;
        // emitCallToken TokenKind::IndexExpr
        argumentPosition = emitCallToken(argumentPosition, TokenKind::IndexExpr, tokBegin, output);
        // next first_argument_square
        goto first_argument_square$no_emit;
    }
    case ']': {
        tokEnd += 1;
        // popScope ScopeKind::Square
        {
            auto result = popScope(scopePosition, ScopeKind::Square);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightSquare);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::BitwiseXorUpdateStmt
            checkLexToken(TokenKind::BitwiseXorUpdateStmt, LexerToken::HatEqual);
            carriedEmitTokenKind = TokenKind::BitwiseXorUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseXorExpr
        checkLexToken(TokenKind::BitwiseXorExpr, LexerToken::Hat);
        carriedEmitTokenKind = TokenKind::BitwiseXorExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '{': {
        tokEnd += 1;
        // emitCallToken TokenKind::Parameterize
        argumentPosition = emitCallToken(argumentPosition, TokenKind::Parameterize, tokBegin, output);
        // next first_argument_brace
        goto first_argument_brace$no_emit;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // popScope ScopeKind::LeftExpr
            {
                auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::BitwiseOrUpdateStmt
            checkLexToken(TokenKind::BitwiseOrUpdateStmt, LexerToken::VertEqual);
            carriedEmitTokenKind = TokenKind::BitwiseOrUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // popScope ScopeKind::LeftExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::LeftExpr);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::LogicalOrUpdateStmt
                checkLexToken(TokenKind::LogicalOrUpdateStmt, LexerToken::VertVertEqual);
                carriedEmitTokenKind = TokenKind::LogicalOrUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::LogicalOrExpr
            checkLexToken(TokenKind::LogicalOrExpr, LexerToken::VertVert);
            carriedEmitTokenKind = TokenKind::LogicalOrExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseOrExpr
        checkLexToken(TokenKind::BitwiseOrExpr, LexerToken::Vert);
        carriedEmitTokenKind = TokenKind::BitwiseOrExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '}': {
        tokEnd += 1;
        // ifScope ScopeKind::BraceInImplExpr
        if (scopePosition[0] == ScopeKind::BraceInImplExpr) {
            // popScope ScopeKind::BraceInImplExpr
            {
                auto result = popScope(scopePosition, ScopeKind::BraceInImplExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // endCall
            argumentPosition = endCall(argumentPosition, output);
            // emitToken TokenKind::EmptyNode
            checkLexToken(TokenKind::EmptyNode, LexerToken::RightBrace);
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next after_impl_expression
            goto after_impl_expression$with_emit;
        }
        // popScope ScopeKind::Brace
        {
            auto result = popScope(scopePosition, ScopeKind::Brace);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightBrace);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '~': {
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        do {
            tokEnd += 1;
        } while (tokEnd[0] >= '0' && tokEnd[0] <= '9');
        // -> error
        goto error$as_then;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '\0':
        // -> error
        goto error$as_then;
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '#':
    case '$':
    case '_':
        goto after_expression$word_case_entry;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
after_expression$word_case_entry:
    {
        auto wordAndPos = readWord(tokEnd, output);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (isKeyword(this_identifier)) {
        // -> error
        goto error$as_then;
    }
LABEL_MAYBE_UNUSED after_expression$identifier_case:
    if (isSpecialIdentifier(this_identifier)) {
    }
    // -> error
    goto error$as_then;

    // LinearState comma_after_expression
comma_after_expression$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::CommaAfterExpression;
    continueState = State::CommaAfterExpression;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Paren
        {
            auto result = popScope(scopePosition, ScopeKind::Paren);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightParen);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (std::string_view(tokEnd, 1) == "]"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Square
        {
            auto result = popScope(scopePosition, ScopeKind::Square);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightSquare);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // ifScope ScopeKind::BraceInImplExpr
        if (scopePosition[0] == ScopeKind::BraceInImplExpr) {
            // popScope ScopeKind::BraceInImplExpr
            {
                auto result = popScope(scopePosition, ScopeKind::BraceInImplExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // endCall
            argumentPosition = endCall(argumentPosition, output);
            // emitToken TokenKind::EmptyNode
            checkLexToken(TokenKind::EmptyNode, LexerToken::RightBrace);
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next after_impl_expression
            goto after_impl_expression$with_emit;
        }
        // popScope ScopeKind::Brace
        {
            auto result = popScope(scopePosition, ScopeKind::Brace);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightBrace);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED comma_after_expression$keyword_check:
            if (this_identifier == words["else"]) {
                // next comma_else
                goto comma_else$no_emit;
            }
            // ifScope ScopeKind::Parameter
            if (scopePosition[0] == ScopeKind::Parameter) {
                // then parameter
                goto parameter$keyword_check;
            }
            // ifScope ScopeKind::VariableType
            if (scopePosition[0] == ScopeKind::VariableType) {
                // popScope ScopeKind::VariableType
                {
                    auto result = popScope(scopePosition, ScopeKind::VariableType);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // popScope ScopeKind::Parameter
                {
                    auto result = popScope(scopePosition, ScopeKind::Parameter);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::Parameter
                scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
                // emitToken TokenKind::ExpressionStmt
                checkLexToken(TokenKind::ExpressionStmt, LexerToken::Invalid);
                emitToken(TokenKind::ExpressionStmt, tokBegin, 0, output);
                // then parameter
                goto parameter$keyword_check;
            }
            // ifScope ScopeKind::RightExpr
            if (scopePosition[0] == ScopeKind::RightExpr) {
                // popScope ScopeKind::RightExpr
                {
                    auto result = popScope(scopePosition, ScopeKind::RightExpr);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // popScope ScopeKind::Parameter
                {
                    auto result = popScope(scopePosition, ScopeKind::Parameter);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::Parameter
                scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
                // emitToken TokenKind::ExpressionStmt
                checkLexToken(TokenKind::ExpressionStmt, LexerToken::Invalid);
                emitToken(TokenKind::ExpressionStmt, tokBegin, 0, output);
                // then parameter
                goto parameter$keyword_check;
            }
            // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace, ScopeKind::BraceInImplExpr
            if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace || scopePosition[0] == ScopeKind::BraceInImplExpr) {
                // then argument
                // callArgument
                argumentPosition = addCallArgument(argumentPosition, Word(), output);
                // emitToken TokenKind::CallArgument
                checkLexToken(TokenKind::CallArgument, LexerToken::Invalid);
                emitToken(TokenKind::CallArgument, tokBegin, 0, output);
                // -> check_designated_argument
                // -> expression
                goto expression$keyword_check;
            }
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED comma_after_expression$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // ifScope ScopeKind::Parameter
        if (scopePosition[0] == ScopeKind::Parameter) {
            // then parameter
            goto parameter$identifier_case;
        }
        // ifScope ScopeKind::VariableType
        if (scopePosition[0] == ScopeKind::VariableType) {
            // popScope ScopeKind::VariableType
            {
                auto result = popScope(scopePosition, ScopeKind::VariableType);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::Parameter
            scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
            // emitToken TokenKind::ExpressionStmt
            checkLexToken(TokenKind::ExpressionStmt, LexerToken::Invalid);
            emitToken(TokenKind::ExpressionStmt, tokBegin, 0, output);
            // then parameter
            goto parameter$identifier_case;
        }
        // ifScope ScopeKind::RightExpr
        if (scopePosition[0] == ScopeKind::RightExpr) {
            // popScope ScopeKind::RightExpr
            {
                auto result = popScope(scopePosition, ScopeKind::RightExpr);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::Parameter
            {
                auto result = popScope(scopePosition, ScopeKind::Parameter);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::Parameter
            scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
            // emitToken TokenKind::ExpressionStmt
            checkLexToken(TokenKind::ExpressionStmt, LexerToken::Invalid);
            emitToken(TokenKind::ExpressionStmt, tokBegin, 0, output);
            // then parameter
            goto parameter$identifier_case;
        }
        // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace, ScopeKind::BraceInImplExpr
        if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace || scopePosition[0] == ScopeKind::BraceInImplExpr) {
            // then argument
            // callArgument
            argumentPosition = addCallArgument(argumentPosition, Word(), output);
            // emitToken TokenKind::CallArgument
            checkLexToken(TokenKind::CallArgument, LexerToken::Invalid);
            emitToken(TokenKind::CallArgument, tokBegin, 0, output);
            // -> check_designated_argument
            goto check_designated_argument$identifier_case;
        }
        // -> error
        goto error$as_then;
    }
    // ifScope ScopeKind::Parameter
    if (scopePosition[0] == ScopeKind::Parameter) {
        // then parameter
        goto parameter$as_then;
    }
    // ifScope ScopeKind::VariableType
    if (scopePosition[0] == ScopeKind::VariableType) {
        // popScope ScopeKind::VariableType
        {
            auto result = popScope(scopePosition, ScopeKind::VariableType);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::Parameter
        scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::Invalid);
        emitToken(TokenKind::ExpressionStmt, tokBegin, 0, output);
        // then parameter
        goto parameter$as_then;
    }
    // ifScope ScopeKind::RightExpr
    if (scopePosition[0] == ScopeKind::RightExpr) {
        // popScope ScopeKind::RightExpr
        {
            auto result = popScope(scopePosition, ScopeKind::RightExpr);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::Parameter
        scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::Invalid);
        emitToken(TokenKind::ExpressionStmt, tokBegin, 0, output);
        // then parameter
        goto parameter$as_then;
    }
    // ifScope ScopeKind::Paren, ScopeKind::Square, ScopeKind::Brace, ScopeKind::BraceInImplExpr
    if (scopePosition[0] == ScopeKind::Paren || scopePosition[0] == ScopeKind::Square || scopePosition[0] == ScopeKind::Brace || scopePosition[0] == ScopeKind::BraceInImplExpr) {
        // then argument
        goto argument$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState comma_else
comma_else$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::CommaElse;
    continueState = State::CommaElse;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 2) == "=>"sv) {
        tokEnd += 2;
        // emitToken TokenKind::CommaElseExpr
        checkLexToken(TokenKind::CommaElseExpr, LexerToken::EqualGreater);
        carriedEmitTokenKind = TokenKind::CommaElseExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState argument
argument$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::Argument;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
argument$as_then:
    continueState = State::Argument;
    savedScopePosition = scopePosition;
    // callArgument
    argumentPosition = addCallArgument(argumentPosition, Word(), output);
    // emitToken TokenKind::CallArgument
    checkLexToken(TokenKind::CallArgument, LexerToken::Invalid);
    emitToken(TokenKind::CallArgument, tokBegin, 0, output);
    // then check_designated_argument
    goto check_designated_argument$as_then;

    // LinearState check_designated_argument
check_designated_argument$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::CheckDesignatedArgument;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
check_designated_argument$as_then:
    continueState = State::CheckDesignatedArgument;
    savedScopePosition = scopePosition;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED check_designated_argument$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // argumentName = this_identifier
        argumentName = this_identifier;
        // emitToken TokenKind::IdentifierExpr
        checkLexToken(TokenKind::IdentifierExpr, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next maybe_designated_argument
        goto maybe_designated_argument$with_emit;
    }
    // then expression
    goto expression$as_then;

    // LinearState maybe_designated_argument
maybe_designated_argument$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
maybe_designated_argument$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::MaybeDesignatedArgument;
    continueState = State::MaybeDesignatedArgument;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // callArgument argumentName
            updateCallArgument(argumentPosition, argumentName, output);
            // discardLastToken
            discardLastToken(output);
            // next expression
            goto expression$no_emit;
        }
    }
    // then after_expression
    goto after_expression$as_then;

    // LinearState first_argument_paren
first_argument_paren$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentParen;
    continueState = State::FirstArgumentParen;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightParen);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::Paren
    scopePosition = pushScope(scopePosition, ScopeKind::Paren);
    // then argument
    goto argument$as_then;

    // LinearState first_argument_square
first_argument_square$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentSquare;
    continueState = State::FirstArgumentSquare;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "]"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightSquare);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::Square
    scopePosition = pushScope(scopePosition, ScopeKind::Square);
    // then argument
    goto argument$as_then;

    // LinearState first_argument_brace
first_argument_brace$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::FirstArgumentBrace;
    continueState = State::FirstArgumentBrace;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightBrace);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::Brace
    scopePosition = pushScope(scopePosition, ScopeKind::Brace);
    // then argument
    goto argument$as_then;

    // LinearState member_access
member_access$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
member_access$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::MemberAccess;
    continueState = State::MemberAccess;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED member_access$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::MemberAccessExpr
        checkLexToken(TokenKind::MemberAccessExpr, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::MemberAccessExpr;
        carriedEmitTokenData = packData1(TokenKind::MemberAccessExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_access
static_access$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::StaticAccess;
    continueState = State::StaticAccess;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED static_access$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::StaticAccessExpr
        checkLexToken(TokenKind::StaticAccessExpr, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::StaticAccessExpr;
        carriedEmitTokenData = packData1(TokenKind::StaticAccessExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState single_or_compound_statement
single_or_compound_statement$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
single_or_compound_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::SingleOrCompoundStatement;
    continueState = State::SingleOrCompoundStatement;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    // then statement
    goto statement$as_then;

    // LinearState after_statement
after_statement$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterStatement;
    continueState = State::AfterStatement;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_statement$keyword_check:
            if (this_identifier == words["else"]) {
                // popScope ScopeKind::IfBranch
                {
                    auto result = popScope(scopePosition, ScopeKind::IfBranch);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::ElseBranch
                scopePosition = pushScope(scopePosition, ScopeKind::ElseBranch);
                // next else_branch
                goto else_branch$no_emit;
            }
            // ifScope ScopeKind::FunctionBody
            if (scopePosition[0] == ScopeKind::FunctionBody) {
                // popScope ScopeKind::FunctionBody
                {
                    auto result = popScope(scopePosition, ScopeKind::FunctionBody);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // then after_declaration
                // endDeclaration
                endDeclaration(output);
                // ifScope ScopeKind::Struct
                if (scopePosition[0] == ScopeKind::Struct) {
                    // then member_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // ifScope ScopeKind::Namespace
                if (scopePosition[0] == ScopeKind::Namespace) {
                    // then namespace_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // ifScope ScopeKind::Enum
                if (scopePosition[0] == ScopeKind::Enum) {
                    // then enum_value_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // -> error
                goto error$as_then;
            }
            // ifScope ScopeKind::Struct, ScopeKind::Namespace, ScopeKind::Enum
            if (scopePosition[0] == ScopeKind::Struct || scopePosition[0] == ScopeKind::Namespace || scopePosition[0] == ScopeKind::Enum) {
                // then after_declaration
                // endDeclaration
                endDeclaration(output);
                // ifScope ScopeKind::Struct
                if (scopePosition[0] == ScopeKind::Struct) {
                    // then member_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // ifScope ScopeKind::Namespace
                if (scopePosition[0] == ScopeKind::Namespace) {
                    // then namespace_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // ifScope ScopeKind::Enum
                if (scopePosition[0] == ScopeKind::Enum) {
                    // then enum_value_declaration
                    // -> templated_declaration
                    goto templated_declaration$keyword_check;
                }
                // -> error
                goto error$as_then;
            }
            // ifScope ScopeKind::IfBranch, ScopeKind::ElseBranch
            if (scopePosition[0] == ScopeKind::IfBranch || scopePosition[0] == ScopeKind::ElseBranch) {
                // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch
                {
                    auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // then statement
                goto statement$keyword_check;
            }
            // ifScope ScopeKind::CompoundStmt
            if (scopePosition[0] == ScopeKind::CompoundStmt) {
                // then statement
                goto statement$keyword_check;
            }
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED after_statement$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // ifScope ScopeKind::FunctionBody
        if (scopePosition[0] == ScopeKind::FunctionBody) {
            // popScope ScopeKind::FunctionBody
            {
                auto result = popScope(scopePosition, ScopeKind::FunctionBody);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // then after_declaration
            // endDeclaration
            endDeclaration(output);
            // ifScope ScopeKind::Struct
            if (scopePosition[0] == ScopeKind::Struct) {
                // then member_declaration
                goto member_declaration$identifier_case;
            }
            // ifScope ScopeKind::Namespace
            if (scopePosition[0] == ScopeKind::Namespace) {
                // then namespace_declaration
                goto namespace_declaration$identifier_case;
            }
            // ifScope ScopeKind::Enum
            if (scopePosition[0] == ScopeKind::Enum) {
                // then enum_value_declaration
                goto enum_value_declaration$identifier_case;
            }
            // -> error
            goto error$as_then;
        }
        // ifScope ScopeKind::Struct, ScopeKind::Namespace, ScopeKind::Enum
        if (scopePosition[0] == ScopeKind::Struct || scopePosition[0] == ScopeKind::Namespace || scopePosition[0] == ScopeKind::Enum) {
            // then after_declaration
            // endDeclaration
            endDeclaration(output);
            // ifScope ScopeKind::Struct
            if (scopePosition[0] == ScopeKind::Struct) {
                // then member_declaration
                goto member_declaration$identifier_case;
            }
            // ifScope ScopeKind::Namespace
            if (scopePosition[0] == ScopeKind::Namespace) {
                // then namespace_declaration
                goto namespace_declaration$identifier_case;
            }
            // ifScope ScopeKind::Enum
            if (scopePosition[0] == ScopeKind::Enum) {
                // then enum_value_declaration
                goto enum_value_declaration$identifier_case;
            }
            // -> error
            goto error$as_then;
        }
        // ifScope ScopeKind::IfBranch, ScopeKind::ElseBranch
        if (scopePosition[0] == ScopeKind::IfBranch || scopePosition[0] == ScopeKind::ElseBranch) {
            // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch
            {
                auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // then statement
            goto statement$identifier_case;
        }
        // ifScope ScopeKind::CompoundStmt
        if (scopePosition[0] == ScopeKind::CompoundStmt) {
            // then statement
            goto statement$identifier_case;
        }
        // -> error
        goto error$as_then;
    }
    // ifScope ScopeKind::FunctionBody
    if (scopePosition[0] == ScopeKind::FunctionBody) {
        // popScope ScopeKind::FunctionBody
        {
            auto result = popScope(scopePosition, ScopeKind::FunctionBody);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // then after_declaration
        goto after_declaration$as_then;
    }
    // ifScope ScopeKind::Struct, ScopeKind::Namespace, ScopeKind::Enum
    if (scopePosition[0] == ScopeKind::Struct || scopePosition[0] == ScopeKind::Namespace || scopePosition[0] == ScopeKind::Enum) {
        // then after_declaration
        goto after_declaration$as_then;
    }
    // ifScope ScopeKind::IfBranch, ScopeKind::ElseBranch
    if (scopePosition[0] == ScopeKind::IfBranch || scopePosition[0] == ScopeKind::ElseBranch) {
        // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch
        {
            auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // then statement
        goto statement$as_then;
    }
    // ifScope ScopeKind::CompoundStmt
    if (scopePosition[0] == ScopeKind::CompoundStmt) {
        // then statement
        goto statement$as_then;
    }
    // then error
    goto error$as_then;

    // SwitchState statement
statement$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
statement$no_emit:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    parseState = State::Statement;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
statement$as_then:
    continueState = State::Statement;
    savedScopePosition = scopePosition;
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, output);
        goto statement$no_emit;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, output);
        goto statement$no_emit;
    }
    case '!': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::LogicalNotExpr
        checkLexToken(TokenKind::LogicalNotExpr, LexerToken::Exclaim);
        carriedEmitTokenKind = TokenKind::LogicalNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '%': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '&': {
        char next = tokEnd[1];
        if (next == '&') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '(': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitCallToken TokenKind::ParenthesizedExpr
        argumentPosition = emitCallToken(argumentPosition, TokenKind::ParenthesizedExpr, tokBegin, output);
        // next first_argument_paren
        goto first_argument_paren$no_emit;
    }
    case ')': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '*': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::DereferenceExpr
        checkLexToken(TokenKind::DereferenceExpr, LexerToken::Star);
        carriedEmitTokenKind = TokenKind::DereferenceExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        char next = tokEnd[1];
        if (next == '+') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // emitToken TokenKind::PreIncrementExpr
            checkLexToken(TokenKind::PreIncrementExpr, LexerToken::PlusPlus);
            carriedEmitTokenKind = TokenKind::PreIncrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::PlusExpr
        checkLexToken(TokenKind::PlusExpr, LexerToken::Plus);
        carriedEmitTokenKind = TokenKind::PlusExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ',': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '-': {
        char next = tokEnd[1];
        if (next == '-') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // emitToken TokenKind::PreDecrementExpr
            checkLexToken(TokenKind::PreDecrementExpr, LexerToken::MinusMinus);
            carriedEmitTokenKind = TokenKind::PreDecrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (next == '>') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::NegateExpr
        checkLexToken(TokenKind::NegateExpr, LexerToken::Minus);
        carriedEmitTokenKind = TokenKind::NegateExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::ImplicitSelfReference
        checkLexToken(TokenKind::ImplicitSelfReference, LexerToken::Point);
        carriedEmitTokenKind = TokenKind::ImplicitSelfReference;
        carriedEmitTokenData = 0;
        // next member_access
        goto member_access$with_emit;
    }
    case '/': {
        char next = tokEnd[1];
        if (next == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            goto statement$no_emit;
        }
        if (next == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            goto statement$no_emit;
        }
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case ':': {
        char next = tokEnd[1];
        if (next == ':') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case ';': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '<': {
        char next = tokEnd[1];
        if (next == '<') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (next == '=') {
            char next = tokEnd[2];
            if (next == '>') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '=': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (next == '>') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '>': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (next == '>') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '[': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case ']': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '^': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '{': {
        tokEnd += 1;
        // pushScope ScopeKind::CompoundStmt
        scopePosition = pushScope(scopePosition, ScopeKind::CompoundStmt);
        // emitToken TokenKind::CompoundStmt
        checkLexToken(TokenKind::CompoundStmt, LexerToken::LeftBrace);
        carriedEmitTokenKind = TokenKind::CompoundStmt;
        carriedEmitTokenData = 0;
        // next statement
        goto statement$with_emit;
    }
    case '|': {
        char next = tokEnd[1];
        if (next == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (next == '|') {
            char next = tokEnd[2];
            if (next == '=') {
                tokEnd += 3;
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // -> expression
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    }
    case '}': {
        tokEnd += 1;
        // ifScope ScopeKind::IfBranch, ScopeKind::ElseBranch
        if (scopePosition[0] == ScopeKind::IfBranch || scopePosition[0] == ScopeKind::ElseBranch) {
            // popScope ScopeKind::IfBranch, ScopeKind::ElseBranch
            {
                auto result = popScope(scopePosition, ScopeKind::IfBranch, ScopeKind::ElseBranch);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
        }
        // popScope ScopeKind::CompoundStmt
        {
            auto result = popScope(scopePosition, ScopeKind::CompoundStmt);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // emitToken TokenKind::EmptyNode
        checkLexToken(TokenKind::EmptyNode, LexerToken::RightBrace);
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_statement
        goto after_statement$with_emit;
    }
    case '~': {
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::BitwiseNotExpr
        checkLexToken(TokenKind::BitwiseNotExpr, LexerToken::Tilde);
        carriedEmitTokenKind = TokenKind::BitwiseNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        do {
            tokEnd += 1;
        } while (tokEnd[0] >= '0' && tokEnd[0] <= '9');
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::LiteralExpr
        checkLexToken(TokenKind::LiteralExpr, LexerToken::Literal);
        carriedEmitTokenKind = TokenKind::LiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::LiteralExpr
        checkLexToken(TokenKind::LiteralExpr, LexerToken::Literal);
        carriedEmitTokenKind = TokenKind::LiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '\0':
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // -> error
        goto error$as_then;
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '#':
    case '$':
    case '_':
        goto statement$word_case_entry;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
statement$word_case_entry:
    {
        auto wordAndPos = readWord(tokEnd, output);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
    if (isKeyword(this_identifier)) {
    LABEL_MAYBE_UNUSED statement$keyword_check:
        if (this_identifier == words["if"]) {
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // pushScope ScopeKind::IfExprOrStmt
            scopePosition = pushScope(scopePosition, ScopeKind::IfExprOrStmt);
            // next expression
            goto expression$no_emit;
        }
        if (this_identifier == words["let"]) {
            // next let_statement
            goto let_statement$no_emit;
        }
        if (this_identifier == words["var"]) {
            // next var_statement
            goto var_statement$no_emit;
        }
        if (this_identifier == words["return"]) {
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::ReturnStmt
            checkLexToken(TokenKind::ReturnStmt, LexerToken::Return);
            carriedEmitTokenKind = TokenKind::ReturnStmt;
            carriedEmitTokenData = 0;
            // next after_return
            goto after_return$with_emit;
        }
        if (this_identifier == words["destroy"]) {
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::DestroyStmt
            checkLexToken(TokenKind::DestroyStmt, LexerToken::Destroy);
            carriedEmitTokenKind = TokenKind::DestroyStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (this_identifier == words["discard"]) {
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::DiscardStmt
            checkLexToken(TokenKind::DiscardStmt, LexerToken::Discard);
            carriedEmitTokenKind = TokenKind::DiscardStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        goto expression$keyword_check;
    }
LABEL_MAYBE_UNUSED statement$identifier_case:
    if (isSpecialIdentifier(this_identifier)) {
    }
    // pushScope ScopeKind::LeftExpr
    scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
    // -> expression
    goto expression$identifier_case;

    // LinearState let_statement
let_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::LetStatement;
    continueState = State::LetStatement;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED let_statement$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::LetValueDecl
        checkLexToken(TokenKind::LetValueDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::LetValueDecl;
        carriedEmitTokenData = packData1(TokenKind::LetValueDecl, this_identifier);
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState var_statement
var_statement$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::VarStatement;
    continueState = State::VarStatement;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED var_statement$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::VarValueDecl
        checkLexToken(TokenKind::VarValueDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::VarValueDecl;
        carriedEmitTokenData = packData1(TokenKind::VarValueDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_return
after_return$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_return$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterReturn;
    continueState = State::AfterReturn;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // popScope ScopeKind::RightExpr
        {
            auto result = popScope(scopePosition, ScopeKind::RightExpr);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // updateKind TokenKind::EmptyReturnStmt
        checkTokenUpdate(output.tokenBuffer.tokens.back().kind(), TokenKind::EmptyReturnStmt);
        output.tokenBuffer.tokens.back().setKind(TokenKind::EmptyReturnStmt);
        // next after_statement
        goto after_statement$no_emit;
    }
    // then expression
    goto expression$as_then;

    // LinearState else_branch
else_branch$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::ElseBranch;
    continueState = State::ElseBranch;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // emitToken TokenKind::ElseStmt
            checkLexToken(TokenKind::ElseStmt, LexerToken::Colon);
            carriedEmitTokenKind = TokenKind::ElseStmt;
            carriedEmitTokenData = 0;
            // next single_or_compound_statement
            goto single_or_compound_statement$with_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState after_simple_variable_declaration_id
after_simple_variable_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_simple_variable_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterSimpleVariableDeclarationId;
    continueState = State::AfterSimpleVariableDeclarationId;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // emitToken TokenKind::VariableType, sema::VariableKind::Let
            checkLexToken(TokenKind::VariableType, LexerToken::Colon);
            carriedEmitTokenKind = TokenKind::VariableType;
            carriedEmitTokenData = packData1(TokenKind::VariableType, sema::VariableKind::Let);
            // next expression
            goto expression$with_emit;
        }
    }
    // then after_variable_declaration_id
    goto after_variable_declaration_id$as_then;

    // LinearState after_variable_declaration_id
after_variable_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_variable_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterVariableDeclarationId;
    continueState = State::AfterVariableDeclarationId;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_variable_declaration_id$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // emitToken TokenKind::VariableType, sema::VariableKind::Let
            checkLexToken(TokenKind::VariableType, LexerToken::Colon);
            carriedEmitTokenKind = TokenKind::VariableType;
            carriedEmitTokenData = packData1(TokenKind::VariableType, sema::VariableKind::Let);
            // next variable_type
            goto variable_type$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AssignStmt
            checkLexToken(TokenKind::AssignStmt, LexerToken::Equal);
            carriedEmitTokenKind = TokenKind::AssignStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::SemiColon);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_statement
        goto after_statement$with_emit;
    }
    if (std::string_view(tokEnd, 1) == ","sv) {
        tokEnd += 1;
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::Parameter
        scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::Comma);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next parameter
        goto parameter$with_emit;
    }
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::RightParen);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_parameters
        goto after_parameters$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState variable_type
variable_type$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
variable_type$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::VariableType;
    continueState = State::VariableType;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "<"sv) {
        char next = tokEnd[1];
        if (next != '<' && next != '=') {
            tokEnd += 1;
            // updateData sema::VariableKind::Generic
            setData1(output.tokenBuffer.tokens.back(), packData1(output.tokenBuffer.tokens.back().kind(), sema::VariableKind::Generic));
            // pushScope ScopeKind::GenericCategoryExpression
            scopePosition = pushScope(scopePosition, ScopeKind::GenericCategoryExpression);
            // emitToken TokenKind::VariableGenericCategory
            checkLexToken(TokenKind::VariableGenericCategory, LexerToken::Less);
            carriedEmitTokenKind = TokenKind::VariableGenericCategory;
            carriedEmitTokenData = 0;
            // next impl_expression
            goto impl_expression$with_emit;
        }
    }
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED variable_type$keyword_check:
            if (this_identifier == words["unique"]) {
                // updateData sema::VariableKind::UniqueReference
                setData1(output.tokenBuffer.tokens.back(), packData1(output.tokenBuffer.tokens.back().kind(), sema::VariableKind::UniqueReference));
                // next after_variable_unique_modifier
                goto after_variable_unique_modifier$no_emit;
            }
            if (this_identifier == words["shared"]) {
                // updateData sema::VariableKind::SharedReference
                setData1(output.tokenBuffer.tokens.back(), packData1(output.tokenBuffer.tokens.back().kind(), sema::VariableKind::SharedReference));
                // next after_variable_shared_modifier
                goto after_variable_shared_modifier$no_emit;
            }
            if (this_identifier == words["const"]) {
                // updateData sema::VariableKind::ConstSharedReference
                setData1(output.tokenBuffer.tokens.back(), packData1(output.tokenBuffer.tokens.back().kind(), sema::VariableKind::ConstSharedReference));
                // next after_variable_const_modifier
                goto after_variable_const_modifier$no_emit;
            }
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED variable_type$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        goto expression$identifier_case;
    }
    // pushScope ScopeKind::VariableType
    scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
    // then expression
    goto expression$as_then;

    // LinearState after_variable_modifier
after_variable_modifier$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterVariableModifier;
    continueState = State::AfterVariableModifier;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_variable_modifier$as_then:
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AssignStmt
            checkLexToken(TokenKind::AssignStmt, LexerToken::Equal);
            carriedEmitTokenKind = TokenKind::AssignStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::SemiColon);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_statement
        goto after_statement$with_emit;
    }
    if (std::string_view(tokEnd, 1) == ","sv) {
        tokEnd += 1;
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::Parameter
        scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::Comma);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next parameter
        goto parameter$with_emit;
    }
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Parameter
        {
            auto result = popScope(scopePosition, ScopeKind::Parameter);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::RightParen);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_parameters
        goto after_parameters$with_emit;
    }
    // pushScope ScopeKind::VariableType
    scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
    // then expression
    goto expression$as_then;

    // LinearState after_variable_unique_modifier
after_variable_unique_modifier$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterVariableUniqueModifier;
    continueState = State::AfterVariableUniqueModifier;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_variable_unique_modifier$keyword_check:
            if (this_identifier == words["const"]) {
                // updateData sema::VariableKind::ConstUniqueReference
                setData1(output.tokenBuffer.tokens.back(), packData1(output.tokenBuffer.tokens.back().kind(), sema::VariableKind::ConstUniqueReference));
                // next after_variable_modifier
                goto after_variable_modifier$no_emit;
            }
            // -> after_variable_modifier
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED after_variable_unique_modifier$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        goto expression$identifier_case;
    }
    // then after_variable_modifier
    goto after_variable_modifier$as_then;

    // LinearState after_variable_shared_modifier
after_variable_shared_modifier$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterVariableSharedModifier;
    continueState = State::AfterVariableSharedModifier;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_variable_shared_modifier$keyword_check:
            if (this_identifier == words["const"]) {
                // updateData sema::VariableKind::ConstSharedReference
                setData1(output.tokenBuffer.tokens.back(), packData1(output.tokenBuffer.tokens.back().kind(), sema::VariableKind::ConstSharedReference));
                // next after_variable_modifier
                goto after_variable_modifier$no_emit;
            }
            // -> after_variable_modifier
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED after_variable_shared_modifier$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        goto expression$identifier_case;
    }
    // then after_variable_modifier
    goto after_variable_modifier$as_then;

    // LinearState after_variable_const_modifier
after_variable_const_modifier$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterVariableConstModifier;
    continueState = State::AfterVariableConstModifier;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_variable_const_modifier$keyword_check:
            if (this_identifier == words["shared"]) {
                // next after_variable_modifier
                goto after_variable_modifier$no_emit;
            }
            if (this_identifier == words["unique"]) {
                // updateData sema::VariableKind::ConstUniqueReference
                setData1(output.tokenBuffer.tokens.back(), packData1(output.tokenBuffer.tokens.back().kind(), sema::VariableKind::ConstUniqueReference));
                // next after_variable_modifier
                goto after_variable_modifier$no_emit;
            }
            // -> after_variable_modifier
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // -> expression
            goto expression$keyword_check;
        }
    LABEL_MAYBE_UNUSED after_variable_const_modifier$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        goto expression$identifier_case;
    }
    // then after_variable_modifier
    goto after_variable_modifier$as_then;

    // LinearState after_parameters
after_parameters$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_parameters$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterParameters;
    continueState = State::AfterParameters;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    // emitToken TokenKind::EmptyNode
    checkLexToken(TokenKind::EmptyNode, LexerToken::Invalid);
    emitToken(TokenKind::EmptyNode, tokBegin, 0, output);
    // ifScope ScopeKind::FunctionParameters
    if (scopePosition[0] == ScopeKind::FunctionParameters) {
        // popScope ScopeKind::FunctionParameters
        {
            auto result = popScope(scopePosition, ScopeKind::FunctionParameters);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // then after_function_parameters
        goto after_function_parameters$as_then;
    }
    // ifScope ScopeKind::TemplateParameters
    if (scopePosition[0] == ScopeKind::TemplateParameters) {
        // popScope ScopeKind::TemplateParameters
        {
            auto result = popScope(scopePosition, ScopeKind::TemplateParameters);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // then after_template_parameters
        goto after_template_parameters$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState first_parameter
first_parameter$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::FirstParameter;
    continueState = State::FirstParameter;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // next after_parameters
        goto after_parameters$no_emit;
    }
    // pushScope ScopeKind::Parameter
    scopePosition = pushScope(scopePosition, ScopeKind::Parameter);
    // then parameter
    goto parameter$as_then;

    // LinearState parameter
parameter$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
parameter$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::Parameter;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
parameter$as_then:
    continueState = State::Parameter;
    savedScopePosition = scopePosition;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED parameter$keyword_check:
            if (this_identifier == words["var"]) {
                // next var_parameter
                goto var_parameter$no_emit;
            }
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED parameter$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::LetValueDecl
        checkLexToken(TokenKind::LetValueDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::LetValueDecl;
        carriedEmitTokenData = packData1(TokenKind::LetValueDecl, this_identifier);
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState var_parameter
var_parameter$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::VarParameter;
    continueState = State::VarParameter;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED var_parameter$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::VarValueDecl
        checkLexToken(TokenKind::VarValueDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::VarValueDecl;
        carriedEmitTokenData = packData1(TokenKind::VarValueDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState impl_expression
impl_expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
impl_expression$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::ImplExpression;
    continueState = State::ImplExpression;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // pushScope ScopeKind::ParenInImplExpr
        scopePosition = pushScope(scopePosition, ScopeKind::ParenInImplExpr);
        // emitToken TokenKind::ParenthesizedExpr
        checkLexToken(TokenKind::ParenthesizedExpr, LexerToken::LeftParen);
        carriedEmitTokenKind = TokenKind::ParenthesizedExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED impl_expression$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::IdentifierExpr
        checkLexToken(TokenKind::IdentifierExpr, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next after_impl_expression
        goto after_impl_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_impl_expression
after_impl_expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_impl_expression$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterImplExpression;
    continueState = State::AfterImplExpression;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 2) == "::"sv) {
        tokEnd += 2;
        // next impl_access_expression
        goto impl_access_expression$no_emit;
    }
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::BraceInImplExpr
        scopePosition = pushScope(scopePosition, ScopeKind::BraceInImplExpr);
        // emitCallToken TokenKind::Parameterize
        argumentPosition = emitCallToken(argumentPosition, TokenKind::Parameterize, tokBegin, output);
        // next argument
        goto argument$no_emit;
    }
    if (std::string_view(tokEnd, 1) == ">"sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // popScope ScopeKind::GenericCategoryExpression
            {
                auto result = popScope(scopePosition, ScopeKind::GenericCategoryExpression);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // next after_variable_modifier
            goto after_variable_modifier$no_emit;
        }
    }
    // ifScope ScopeKind::StructImplExpression
    if (scopePosition[0] == ScopeKind::StructImplExpression) {
        // popScope ScopeKind::StructImplExpression
        {
            auto result = popScope(scopePosition, ScopeKind::StructImplExpression);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // then after_struct_declaration_id
        goto after_struct_declaration_id$as_then;
    }
    // ifScope ScopeKind::FunctionImplExpression
    if (scopePosition[0] == ScopeKind::FunctionImplExpression) {
        // popScope ScopeKind::FunctionImplExpression
        {
            auto result = popScope(scopePosition, ScopeKind::FunctionImplExpression);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // then after_function_declaration_id
        goto after_function_declaration_id$as_then;
    }
    // ifScope ScopeKind::EnumImplExpression
    if (scopePosition[0] == ScopeKind::EnumImplExpression) {
        // popScope ScopeKind::EnumImplExpression
        {
            auto result = popScope(scopePosition, ScopeKind::EnumImplExpression);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // then after_enum_declaration_id
        goto after_enum_declaration_id$as_then;
    }
    // ifScope ScopeKind::GlobalImplExpression
    if (scopePosition[0] == ScopeKind::GlobalImplExpression) {
        // popScope ScopeKind::GlobalImplExpression
        {
            auto result = popScope(scopePosition, ScopeKind::GlobalImplExpression);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::Invalid);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState impl_access_expression
impl_access_expression$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::ImplAccessExpression;
    continueState = State::ImplAccessExpression;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED impl_access_expression$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // emitToken TokenKind::StaticAccessExpr
        checkLexToken(TokenKind::StaticAccessExpr, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::StaticAccessExpr;
        carriedEmitTokenData = packData1(TokenKind::StaticAccessExpr, this_identifier);
        // next after_impl_expression
        goto after_impl_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState no_declaration
no_declaration$as_then:
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // popScope ScopeKind::Namespace, ScopeKind::Struct, ScopeKind::Enum
        {
            auto result = popScope(scopePosition, ScopeKind::Namespace, ScopeKind::Struct, ScopeKind::Enum);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // next after_declaration
        goto after_declaration$no_emit;
    }
    if (tokEnd[0] == '\0') {
        emitToken(TokenKind::EOS, tokBegin, 0, output);
        emitWhitespace(WhitespaceKind::EOS, tokBegin, tokEnd, output);
        goto exit;
    }
    // then error
    goto error$as_then;

    // LinearState namespace_declaration
namespace_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::NamespaceDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
namespace_declaration$as_then:
    continueState = State::NamespaceDeclaration;
    savedScopePosition = scopePosition;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
    LABEL_MAYBE_UNUSED namespace_declaration$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["namespace"]) {
                // next namespace_declaration_id
                goto namespace_declaration_id$no_emit;
            }
            if (this_identifier == words["template"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::TemplateAttribute
                checkLexToken(TokenKind::TemplateAttribute, LexerToken::Template);
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                checkLexToken(TokenKind::IncompleteAttribute, LexerToken::Incomplete);
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::VirtualAttribute
                checkLexToken(TokenKind::VirtualAttribute, LexerToken::Virtual);
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // -> templated_declaration
        goto templated_declaration$identifier_case;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState namespace_declaration_id
namespace_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::NamespaceDeclarationId;
    continueState = State::NamespaceDeclarationId;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED namespace_declaration_id$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // rememberDeclarationBegin
        declarationBegin = output.tokenBuffer.currentToken();
        // commitDeclaration DeclarationKind::Namespace, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Namespace>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::NamespaceDecl
        checkLexToken(TokenKind::NamespaceDecl, LexerToken::Identifier);
        emitToken(TokenKind::NamespaceDecl, tokBegin, packData1(TokenKind::NamespaceDecl, this_identifier), output);
        // updateSecondaryData this_declaration
        setData2(output.tokenBuffer.tokens.back(), packData2(output.tokenBuffer.tokens.back().kind(), this_declaration));
        // next after_namespace_declaration_id
        goto after_namespace_declaration_id$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_namespace_declaration_id
after_namespace_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterNamespaceDeclarationId;
    continueState = State::AfterNamespaceDeclarationId;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // next namespace_declaration_body
            goto namespace_declaration_body$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState namespace_declaration_body
namespace_declaration_body$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::NamespaceDeclarationBody;
    continueState = State::NamespaceDeclarationBody;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::Namespace
        scopePosition = pushScope(scopePosition, ScopeKind::Namespace);
        // next namespace_declaration
        goto namespace_declaration$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState templated_declaration
templated_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED templated_declaration$keyword_check:
            if (this_identifier == words["static"]) {
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next after_static
                goto after_static$no_emit;
            }
            // -> no_declaration
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED templated_declaration$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["template"]) {
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::TemplateAttribute
                checkLexToken(TokenKind::TemplateAttribute, LexerToken::Template);
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                checkLexToken(TokenKind::IncompleteAttribute, LexerToken::Incomplete);
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::VirtualAttribute
                checkLexToken(TokenKind::VirtualAttribute, LexerToken::Virtual);
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // -> no_declaration
        // -> error
        goto error$as_then;
    }
    // then no_declaration
    goto no_declaration$as_then;

    // LinearState templated_declaration_with_attributes
templated_declaration_with_attributes$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
templated_declaration_with_attributes$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::TemplatedDeclarationWithAttributes;
    continueState = State::TemplatedDeclarationWithAttributes;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
templated_declaration_with_attributes$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED templated_declaration_with_attributes$keyword_check:
            if (this_identifier == words["static"]) {
                // next after_static
                goto after_static$no_emit;
            }
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED templated_declaration_with_attributes$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["template"]) {
                // emitToken TokenKind::TemplateAttribute
                checkLexToken(TokenKind::TemplateAttribute, LexerToken::Template);
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // emitToken TokenKind::IncompleteAttribute
                checkLexToken(TokenKind::IncompleteAttribute, LexerToken::Incomplete);
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // emitToken TokenKind::VirtualAttribute
                checkLexToken(TokenKind::VirtualAttribute, LexerToken::Virtual);
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // -> error
        goto error$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState after_template
after_template$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_template$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterTemplate;
    continueState = State::AfterTemplate;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // pushScope ScopeKind::TemplateParameters
        scopePosition = pushScope(scopePosition, ScopeKind::TemplateParameters);
        // next first_parameter
        goto first_parameter$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_template_parameters
after_template_parameters$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterTemplateParameters;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_template_parameters$as_then:
    continueState = State::AfterTemplateParameters;
    savedScopePosition = scopePosition;
    // then templated_declaration_with_attributes
    goto templated_declaration_with_attributes$as_then;

    // LinearState function_declaration_id
function_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::FunctionDeclarationId;
    continueState = State::FunctionDeclarationId;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED function_declaration_id$keyword_check:
            if (this_identifier == words["impl"]) {
                // commitImplDeclaration DeclarationKind::Function
                this_declaration = commitImplDeclaration<DeclarationKind::Function>(tokBegin, declarationBegin, output);
                // pushScope ScopeKind::FunctionImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::FunctionImplExpression);
                // emitToken TokenKind::FunctionImplDecl
                checkLexToken(TokenKind::FunctionImplDecl, LexerToken::Impl);
                carriedEmitTokenKind = TokenKind::FunctionImplDecl;
                carriedEmitTokenData = 0;
                // next impl_expression
                goto impl_expression$with_emit;
            }
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED function_declaration_id$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::Function, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Function>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::FunctionDecl
        checkLexToken(TokenKind::FunctionDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::FunctionDecl;
        carriedEmitTokenData = packData1(TokenKind::FunctionDecl, this_identifier);
        // next after_function_declaration_id
        goto after_function_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_function_declaration_id
after_function_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_function_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterFunctionDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_function_declaration_id$as_then:
    continueState = State::AfterFunctionDeclarationId;
    savedScopePosition = scopePosition;
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // pushScope ScopeKind::FunctionParameters
        scopePosition = pushScope(scopePosition, ScopeKind::FunctionParameters);
        // next first_parameter
        goto first_parameter$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_function_parameters
after_function_parameters$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterFunctionParameters;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_function_parameters$as_then:
    continueState = State::AfterFunctionParameters;
    savedScopePosition = scopePosition;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // pushScope ScopeKind::FunctionBody
            scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
            // emitToken TokenKind::FunctionBody
            checkLexToken(TokenKind::FunctionBody, LexerToken::Colon);
            carriedEmitTokenKind = TokenKind::FunctionBody;
            carriedEmitTokenData = 0;
            // next single_or_compound_statement
            goto single_or_compound_statement$with_emit;
        }
    }
    if (std::string_view(tokEnd, 2) == "->"sv) {
        tokEnd += 2;
        // pushScope ScopeKind::ReturnType
        scopePosition = pushScope(scopePosition, ScopeKind::ReturnType);
        // emitToken TokenKind::ReturnType
        checkLexToken(TokenKind::ReturnType, LexerToken::MinusGreater);
        carriedEmitTokenKind = TokenKind::ReturnType;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    if (std::string_view(tokEnd, 2) == "=>"sv) {
        tokEnd += 2;
        // pushScope ScopeKind::FunctionBody
        scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // emitToken TokenKind::BodyExpr
        checkLexToken(TokenKind::BodyExpr, LexerToken::EqualGreater);
        carriedEmitTokenKind = TokenKind::BodyExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState struct_declaration_id
struct_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::StructDeclarationId;
    continueState = State::StructDeclarationId;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED struct_declaration_id$keyword_check:
            if (this_identifier == words["impl"]) {
                // commitImplDeclaration DeclarationKind::Struct
                this_declaration = commitImplDeclaration<DeclarationKind::Struct>(tokBegin, declarationBegin, output);
                // pushScope ScopeKind::StructImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::StructImplExpression);
                // emitToken TokenKind::StructImplDecl
                checkLexToken(TokenKind::StructImplDecl, LexerToken::Impl);
                carriedEmitTokenKind = TokenKind::StructImplDecl;
                carriedEmitTokenData = 0;
                // next impl_expression
                goto impl_expression$with_emit;
            }
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED struct_declaration_id$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::Struct, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Struct>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::StructDecl
        checkLexToken(TokenKind::StructDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::StructDecl;
        carriedEmitTokenData = packData1(TokenKind::StructDecl, this_identifier);
        // next after_struct_declaration_id
        goto after_struct_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_struct_declaration_id
after_struct_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_struct_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterStructDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_struct_declaration_id$as_then:
    continueState = State::AfterStructDeclarationId;
    savedScopePosition = scopePosition;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // next struct_declaration_body
            goto struct_declaration_body$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState struct_declaration_body
struct_declaration_body$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::StructDeclarationBody;
    continueState = State::StructDeclarationBody;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::Struct
        scopePosition = pushScope(scopePosition, ScopeKind::Struct);
        // next member_declaration
        goto member_declaration$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState member_declaration
member_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::MemberDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
member_declaration$as_then:
    continueState = State::MemberDeclaration;
    savedScopePosition = scopePosition;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
    LABEL_MAYBE_UNUSED member_declaration$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["base"]) {
                // pushScope ScopeKind::BaseTypeExpr
                scopePosition = pushScope(scopePosition, ScopeKind::BaseTypeExpr);
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // commitDeclaration DeclarationKind::BaseMember
                this_declaration = commitDeclaration<DeclarationKind::BaseMember>(Word(), tokBegin, declarationBegin, output);
                // emitToken TokenKind::BaseMemberDecl
                checkLexToken(TokenKind::BaseMemberDecl, LexerToken::Base);
                carriedEmitTokenKind = TokenKind::BaseMemberDecl;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            if (this_identifier == words["template"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::TemplateAttribute
                checkLexToken(TokenKind::TemplateAttribute, LexerToken::Template);
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                checkLexToken(TokenKind::IncompleteAttribute, LexerToken::Incomplete);
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::VirtualAttribute
                checkLexToken(TokenKind::VirtualAttribute, LexerToken::Virtual);
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // rememberDeclarationBegin
        declarationBegin = output.tokenBuffer.currentToken();
        // commitDeclaration DeclarationKind::Member, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Member>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::MemberDecl
        checkLexToken(TokenKind::MemberDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::MemberDecl;
        carriedEmitTokenData = packData1(TokenKind::MemberDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState enum_declaration_id
enum_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::EnumDeclarationId;
    continueState = State::EnumDeclarationId;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED enum_declaration_id$keyword_check:
            if (this_identifier == words["impl"]) {
                // commitImplDeclaration DeclarationKind::Enum
                this_declaration = commitImplDeclaration<DeclarationKind::Enum>(tokBegin, declarationBegin, output);
                // pushScope ScopeKind::EnumImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::EnumImplExpression);
                // emitToken TokenKind::EnumImplDecl
                checkLexToken(TokenKind::EnumImplDecl, LexerToken::Impl);
                carriedEmitTokenKind = TokenKind::EnumImplDecl;
                carriedEmitTokenData = 0;
                // next impl_expression
                goto impl_expression$with_emit;
            }
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED enum_declaration_id$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::Enum, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Enum>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::EnumDecl
        checkLexToken(TokenKind::EnumDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::EnumDecl;
        carriedEmitTokenData = packData1(TokenKind::EnumDecl, this_identifier);
        // next after_enum_declaration_id
        goto after_enum_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_enum_declaration_id
after_enum_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_enum_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterEnumDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_enum_declaration_id$as_then:
    continueState = State::AfterEnumDeclarationId;
    savedScopePosition = scopePosition;
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // next enum_declaration_body
            goto enum_declaration_body$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState enum_declaration_body
enum_declaration_body$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::EnumDeclarationBody;
    continueState = State::EnumDeclarationBody;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "{"sv) {
        tokEnd += 1;
        // pushScope ScopeKind::Enum
        scopePosition = pushScope(scopePosition, ScopeKind::Enum);
        // next enum_value_declaration
        goto enum_value_declaration$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState enum_value_declaration
enum_value_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::EnumValueDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
enum_value_declaration$as_then:
    continueState = State::EnumValueDeclaration;
    savedScopePosition = scopePosition;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> templated_declaration
            goto templated_declaration$keyword_check;
        }
    LABEL_MAYBE_UNUSED enum_value_declaration$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["template"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::TemplateAttribute
                checkLexToken(TokenKind::TemplateAttribute, LexerToken::Template);
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            }
            if (this_identifier == words["incomplete"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                checkLexToken(TokenKind::IncompleteAttribute, LexerToken::Incomplete);
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["virtual"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::VirtualAttribute
                checkLexToken(TokenKind::VirtualAttribute, LexerToken::Virtual);
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            }
            if (this_identifier == words["fn"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            }
            if (this_identifier == words["struct"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            }
            if (this_identifier == words["enum"]) {
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            }
        }
        // rememberDeclarationBegin
        declarationBegin = output.tokenBuffer.currentToken();
        // commitDeclaration DeclarationKind::EnumValue, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::EnumValue>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::ImplicitEnumValueDecl
        checkLexToken(TokenKind::ImplicitEnumValueDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::ImplicitEnumValueDecl;
        carriedEmitTokenData = packData1(TokenKind::ImplicitEnumValueDecl, this_identifier);
        // next after_enum_value_declaration_id
        goto after_enum_value_declaration_id$with_emit;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState after_enum_value_declaration_id
after_enum_value_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_enum_value_declaration_id$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterEnumValueDeclarationId;
    continueState = State::AfterEnumValueDeclarationId;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // updateKind TokenKind::ExplicitEnumValueDecl
            checkTokenUpdate(output.tokenBuffer.tokens.back().kind(), TokenKind::ExplicitEnumValueDecl);
            output.tokenBuffer.tokens.back().setKind(TokenKind::ExplicitEnumValueDecl);
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AssignStmt
            checkLexToken(TokenKind::AssignStmt, LexerToken::Equal);
            carriedEmitTokenKind = TokenKind::AssignStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // emitToken TokenKind::ExpressionStmt
        checkLexToken(TokenKind::ExpressionStmt, LexerToken::SemiColon);
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_declaration
        goto after_declaration$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_static
after_static$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterStatic;
    continueState = State::AfterStatic;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
        LABEL_MAYBE_UNUSED after_static$keyword_check:
            if (this_identifier == words["var"]) {
                // next static_var_variable_declaration
                goto static_var_variable_declaration$no_emit;
            }
            if (this_identifier == words["impl"]) {
                // commitImplDeclaration DeclarationKind::StaticVariable
                this_declaration = commitImplDeclaration<DeclarationKind::StaticVariable>(tokBegin, declarationBegin, output);
                // pushScope ScopeKind::GlobalImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::GlobalImplExpression);
                // emitToken TokenKind::GlobalImplDecl
                checkLexToken(TokenKind::GlobalImplDecl, LexerToken::Impl);
                carriedEmitTokenKind = TokenKind::GlobalImplDecl;
                carriedEmitTokenData = 0;
                // next impl_expression
                goto impl_expression$with_emit;
            }
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED after_static$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
            if (this_identifier == words["open"]) {
                // next static_open_variable_declaration
                goto static_open_variable_declaration$no_emit;
            }
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, output);
        // setGlobalKind GlobalKind::Let
        setGlobalKind(output, GlobalKind::Let);
        // emitToken TokenKind::GlobalDecl
        checkLexToken(TokenKind::GlobalDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::GlobalDecl;
        carriedEmitTokenData = packData1(TokenKind::GlobalDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_var_variable_declaration
static_var_variable_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::StaticVarVariableDeclaration;
    continueState = State::StaticVarVariableDeclaration;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED static_var_variable_declaration$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, output);
        // setGlobalKind GlobalKind::Var
        setGlobalKind(output, GlobalKind::Var);
        // emitToken TokenKind::GlobalDecl
        checkLexToken(TokenKind::GlobalDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::GlobalDecl;
        carriedEmitTokenData = packData1(TokenKind::GlobalDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_open_variable_declaration
static_open_variable_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::StaticOpenVariableDeclaration;
    continueState = State::StaticOpenVariableDeclaration;
    savedScopePosition = scopePosition;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    if (isWordFirstCharacter(tokEnd[0])) {
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
        if (isKeyword(this_identifier)) {
            // -> error
            goto error$as_then;
        }
    LABEL_MAYBE_UNUSED static_open_variable_declaration$identifier_case:
        if (isSpecialIdentifier(this_identifier)) {
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, output);
        // setGlobalKind GlobalKind::OpenLet
        setGlobalKind(output, GlobalKind::OpenLet);
        // emitToken TokenKind::GlobalDecl
        checkLexToken(TokenKind::GlobalDecl, LexerToken::Identifier);
        carriedEmitTokenKind = TokenKind::GlobalDecl;
        carriedEmitTokenData = packData1(TokenKind::GlobalDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_declaration
after_declaration$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_declaration$no_emit:
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
    parseState = State::AfterDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_declaration$as_then:
    continueState = State::AfterDeclaration;
    savedScopePosition = scopePosition;
    // endDeclaration
    endDeclaration(output);
    // ifScope ScopeKind::Struct
    if (scopePosition[0] == ScopeKind::Struct) {
        // then member_declaration
        goto member_declaration$as_then;
    }
    // ifScope ScopeKind::Namespace
    if (scopePosition[0] == ScopeKind::Namespace) {
        // then namespace_declaration
        goto namespace_declaration$as_then;
    }
    // ifScope ScopeKind::Enum
    if (scopePosition[0] == ScopeKind::Enum) {
        // then enum_value_declaration
        goto enum_value_declaration$as_then;
    }
    // then error
    goto error$as_then;


pop_scope_failed:
    println("Pop scope failed in state '{}' at \"{:.12}\"", nameString(parseState), tokBegin);
    return { ReturnStatus::ScopeError, parseState, continueState, declarationBegin, argumentName, tokBegin, savedScopePosition, argumentPosition };
error$as_then:
    println("Reached error state after '{}' at \"{:.12}\"", nameString(parseState), tokBegin);
    return { ReturnStatus::UnhandledCase, parseState, continueState, declarationBegin, argumentName, tokBegin, savedScopePosition, argumentPosition };
reached_token_limit:
    return { ReturnStatus::Ready, parseState, parseState, declarationBegin, argumentName, tokBegin, scopePosition, argumentPosition };
exit:
    return { ReturnStatus::EOS, parseState, continueState, declarationBegin, argumentName, tokBegin, scopePosition, argumentPosition };
}

ReturnStatus Parser::parse(sema::Context& output, int_t tokenLimit) {
    m_state = parseImpl(m_state, output, tokenLimit);
    return status();
}

ReturnStatus SimpleParser::parse(SimpleOutput& output, int_t tokenLimit) {
    Parser::InternalState inState = {
        m_state.status,
        m_state.state,
        m_state.continueState,
        TokenHandle(),
        Word(),
        m_state.sourcePosition,
        m_state.scopePosition,
        nullptr
    };
    auto outState = Parser::parseImpl(inState, output, tokenLimit);
    m_state = {
        outState.status,
        outState.state,
        outState.continueState,
        outState.sourcePosition,
        outState.scopePosition
    };
    return status();
}

LexerToken Parser::lexToken() {
    SimpleOutput output;
    return lexImpl(m_state.sourcePosition, output);
}

LexerToken SimpleParser::lexToken() {
    SimpleOutput output;
    return lexImpl(m_state.sourcePosition, output);
}

SourceLocation Parser::location(sema::Context& context) const {
    return locationInCurrentLine(sourcePosition(), context);
}

}