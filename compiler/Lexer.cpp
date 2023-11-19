#include "Parser.h"
#include <array>
#include <ranges>

std::string_view toSmallString(Token token) {
    switch (token) {
    case Token::Invalid:
        return "Invalid";
    default:
        VERIFY_NOT_REACHED();

#define PUNC(t, sp) \
    case Token::t:  \
        return sp;
#define TOKEN(t)   \
    case Token::t: \
        return #t;

        ENUMERTATE_TOKENS
        ENUMERATE_PUNCTUATION_TOKENS

#undef TOKEN
#undef PUNC
    }
    // clang-format on
}
std::string_view nameString(Token token) {
    switch (token) {
    case Token::Invalid:
        return "Invalid";
    default:
        VERIFY_NOT_REACHED();

#define PUNC(t, sp) \
    case Token::t:  \
        return #t;
#define TOKEN(t)   \
    case Token::t: \
        return #t;

        ENUMERTATE_TOKENS
        ENUMERATE_PUNCTUATION_TOKENS

#undef TOKEN
#undef PUNC
    }
}

// advances offset to the next non-whitespace character
static void skipTabsAndSpaces(std::string_view sourceBuffer, int_t& sourceOffset) {
    while (sourceBuffer[sourceOffset] == ' ' || sourceBuffer[sourceOffset] == '\t')
        sourceOffset += 1;
}

static bool isEndOfLineCharacter(uint8_t c) {
    return c == '\n' || c == '\r';
}
// advances offset to the next new line character
static void skipToEndOfLine(std::string_view sourceBuffer, int_t& sourceOffset) {
    while (!isEndOfLineCharacter(sourceBuffer[sourceOffset]))
        sourceOffset += 1;
}

// advances offset to the next '</'
static void skipToEndOfBlockComment(std::string_view sourceBuffer, int_t& sourceOffset) {
    while (sourceBuffer[sourceOffset] != '\0'
        && !(sourceBuffer[sourceOffset] == '<' && sourceBuffer[sourceOffset + 1] == '/')) {
        sourceOffset += 1;
    }
}

static void skipToEndOfCharacterLiteral(std::string_view sourceBuffer, int_t& sourceOffset) {
    for (;;) {
        auto c = sourceBuffer[sourceOffset];
        if (c == '\'' || c == '\n' || c == '\r' || c == '\0')
            break;
        sourceOffset += 1;
    }
}

static bool isBulkWordCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

static constexpr Token binaryToUpdateOp(Token in) {
    return Token(std::to_underlying(in) - std::to_underlying(Token::Plus)
        + std::to_underlying(Token::PlusEqual));
}

namespace lexer_lookup_table {

struct CharacterRange {
    char first;
    char last;
    constexpr CharacterRange()
        : first('\0'), last(127) { }
    constexpr CharacterRange(char c)
        : first(c), last(c) { }
    constexpr CharacterRange(char first, char last)
        : first(first), last(last) { }
    constexpr auto range() const { return std::views::iota((size_t)first, (size_t)last + 1); }
    constexpr auto begin() const { return range().begin(); }
    constexpr auto end() const { return range().end(); }
};

struct Punctuation {
    Token token;
    std::string_view spelling;
};
constexpr auto sortedPunctuations() {
#define PUNC(name, spelling) Punctuation { Token::name, spelling },
    std::array punctuations = { ENUMERATE_PUNCTUATION_TOKENS };
#undef PUNC
    std::sort(punctuations.begin(), punctuations.end(),
        [](Punctuation l, Punctuation r) { return l.spelling.length() < r.spelling.length(); });

    return punctuations;
}

enum class Result : uint8_t {
    Invalid,
    NewlineLF,
    NewlineCRLF,
    LineComment,
    BlockComment,
    PunctuationPossiblyDoubleArrow,
    Word,
    NumbericLiteral,
    CharacterLiteral,
};

struct Input {
    CharacterRange c0;
    CharacterRange c1;
    Result result;
    constexpr Input(CharacterRange c0, Result result)
        : c0(c0), c1(), result(result) { }
    constexpr Input(CharacterRange c0, CharacterRange c1, Result result)
        : c0(c0), c1(c1), result(result) { }
};

constexpr auto makeTable(std::initializer_list<Input> inputs) {
    std::array<uint8_t, 128 * 128> output = {};

    auto put = [&output](size_t t0, size_t t1, uint8_t r) constexpr {
        output[(t0 << 7) | t1] = r;
    };
    auto encodePunctuation = [](Punctuation p) constexpr -> uint8_t {
        VERIFY(p.spelling.length() == 1 || p.spelling.length() == 2);
        return std::to_underlying(p.token) | (p.spelling.length() << 6);
    };
    auto encodePunctuationExtenededByEqual = [](Token baseToken) constexpr -> uint8_t {
        return std::to_underlying(baseToken) | (uint8_t)0b1100'0000;
    };

    // punctuations
    auto punctuations = sortedPunctuations();
    for (auto punc : punctuations) {
        if (punc.spelling.length() > 2)
            continue;
        std::vector<Punctuation> possibleExtensions;
        for (auto other : punctuations) {
            if (other.spelling.length() > punc.spelling.length() && other.spelling.starts_with(punc.spelling))
                possibleExtensions.push_back(other);
        }
        auto hasExtension = [&possibleExtensions, &punc](char c) {
            return std::find_if(possibleExtensions.begin(),
                       possibleExtensions.end(),
                       [&](Punctuation ext) { return ext.spelling[punc.spelling.length()] == c; })
                != possibleExtensions.end();
        };
        if (punc.spelling.length() == 1) {
            for (auto c : CharacterRange()) {
                if (hasExtension(c)) {
                    // This will be written later.
                } else {
                    put(punc.spelling[0], c, encodePunctuation(punc));
                }
            }
        } else if (possibleExtensions.empty()) {
            put(punc.spelling[0], punc.spelling[1], encodePunctuation(punc));
        } else {
            VERIFY(possibleExtensions.size() == 1);
            auto extension = possibleExtensions.front();
            VERIFY(extension.spelling.length() == 3);
            char extensionChar = extension.spelling[2];
            if (extensionChar == '=') {
                VERIFY(binaryToUpdateOp(punc.token) == extension.token);
                put(punc.spelling[0], punc.spelling[1], encodePunctuationExtenededByEqual(punc.token));
            } else if (extensionChar == '>') {
                VERIFY(extension.token == Token::DoubleArrow);
                put(punc.spelling[0], punc.spelling[1], std::to_underlying(Result::PunctuationPossiblyDoubleArrow));
            } else
                VERIFY_NOT_REACHED();
        }
    }

    // other tokens
    for (Input in : inputs) {
        for (auto c0 : in.c0) {
            for (auto c1 : in.c1)
                put(c0, c1, std::to_underlying(in.result));
        }
    }

    return output;
}

constexpr auto table = makeTable({
    { { '0', '9' }, Result::NumbericLiteral },
    { '\'', Result::CharacterLiteral },

    { '$', Result::Word },
    { '#', Result::Word },
    { '_', Result::Word },
    { { 'a', 'z' }, Result::Word },
    { { 'A', 'Z' }, Result::Word },

    { '/', '/', Result::LineComment },
    { '/', '>', Result::BlockComment },
    { '\n', Result::NewlineLF },
    { '\r', '\n', Result::NewlineCRLF },
});

constexpr Result lookup(char c0, char c1) {
    return (Result)table[((size_t)c0 << 7) | (size_t)c1];
}

}

bool TokenWithData::valid() const {
    if (tok <= Token::Invalid || tok >= Token::COUNT)
        return false;
    if (tok == Token::Word)
        return std::holds_alternative<Word>(tokData);
    if (tok == Token::NumericLiteral)
        return std::holds_alternative<NumericLiteral>(tokData);
    if (tok == Token::CharacterLiteral)
        return std::holds_alternative<CharacterLiteral>(tokData);
    return std::holds_alternative<std::monostate>(tokData);
}
bool Lexer::valid() const {
    if (sourceBuffer.empty())
        return false;
    if (*sourceBuffer.end() != '\0' && sourceBuffer.back() != '\0')
        return false;
    if (tokBegin > sourceOffset)
        return false;
    return TokenWithData::valid();
}

std::string_view Lexer::source(int_t begin, int_t end) const {
    return { sourceBuffer.begin() + begin, sourceBuffer.begin() + end };
}
std::string_view Lexer::tokCommentSource() const {
    auto token = tokenStream.back();
    VERIFY(isComment(token.token()));
    if (token.token() == Token::BlockComment)
        return source(token.begin() + 2, token.end() - 2);
    if (token.token() == Token::LineComment)
        return source(token.begin() + 2, token.end());
    VERIFY_NOT_REACHED();
}

SingleTokenSourceRange Lexer::tokRange() const {
    if (cachedNextToken.tok == Token::Invalid)
        return SingleTokenSourceRange(tokenStream.size() - 1);
    int_t index = tokenStream.size() - 2;
    while (isWhitespaceToken(tokenStream[index].token())) {
        index -= 1;
        VERIFY(index >= 0);
    }
    return SingleTokenSourceRange(index);
}

static std::pair<const StreamToken*, uint32_t> findInStream(const Lexer* lex, LocalSourceLocation loc) {
    auto iter = lex->tokenStream.data() + loc.tokenStreamOffset;
    auto offset = loc.isAtEnd ? iter->end() : iter->begin();
    return { iter, offset };
};

SourcePosition Lexer::sourcePosition(LocalSourceLocation loc) const {
    auto [iter, offset] = findInStream(this, loc);
    while (iter->token() != Token::Newline)
        --iter;
    return { .line = iter->lineNumber(), .column = offset - iter->begin() + 1 };
}

void Lexer::formatLine(std::ostream& out, LocalSourceRange highlight) const {
    auto [beginIter, highlightBegin] = findInStream(this, highlight.first());
    while (beginIter->token() != Token::Newline)
        --beginIter;
    auto [endIter, highlightEnd] = findInStream(this, highlight.last());
    do {
        ++endIter;
    } while (endIter->token() != Token::Newline && endIter < tokenStream.end());
    --endIter;
    uint32_t beginOffset = (beginIter + 1)->begin();
    uint32_t endOffset = endIter->end();
    out << "    " << sourceBuffer.substr(beginOffset, endOffset - beginOffset) << '\n';
    std::string highlightStr;
    highlightStr.resize(highlightBegin - beginOffset, ' ');
    highlightStr.resize(highlightEnd - beginOffset, '^');
    out << "    " << highlightStr << '\n';
}

void Lexer::setSource(std::string_view source) {
    // reset all fields
    (LexerState&)* this = { source };

    if (source.empty())
        return;
    VERIFY(source[source.length()] == '\0');
    VERIFY(source[source.length() - 1] != '\0');
}

void Lexer::nextToken() {

#define HANDLE_LEXER_ACTION(a)                                         \
    {                                                                  \
        ErrorHandler::LexerAction action = a;                          \
        if (action == ErrorHandler::LexerAction::Retry) {              \
            continue;                                                  \
        } else if (action == ErrorHandler::LexerAction::AcceptState) { \
            break;                                                     \
        } else {                                                       \
            VERIFY_NOT_REACHED();                                      \
        }                                                              \
    }

    if (cachedNextToken.tok != Token::Invalid) {
        (TokenWithData&)* this = cachedNextToken;
        cachedNextToken = {};
        return;
    }

    for (;;) {
        skipTabsAndSpaces(sourceBuffer, sourceOffset);

        tokBegin = sourceOffset;
        tok = Token::Invalid;
        tokData = {};

        char c0 = sourceBuffer[sourceOffset + 0];
        char c1 = sourceBuffer[sourceOffset + 1];
        if (c0 < 0 || c0 > 127) [[unlikely]] {
            HANDLE_LEXER_ACTION(errorHandler->invalidCharacter(this, c0));
        }
        if (c1 < 0 || c1 > 127) [[unlikely]] {
            HANDLE_LEXER_ACTION(errorHandler->invalidCharacter(this, c1));
        }

        auto lookupResult = lexer_lookup_table::lookup(c0, c1);

        if (std::to_underlying(lookupResult) >= (uint8_t)0b1100'0000) {
            Token baseToken = Token(std::to_underlying(lookupResult) & (uint8_t)0b0011'1111);
            bool b = sourceBuffer[sourceOffset + 2] == '=';
            tok = b ? binaryToUpdateOp(baseToken) : baseToken;
            sourceOffset += b ? 3 : 2;
        } else if (std::to_underlying(lookupResult) >= (uint8_t)0b0100'0000) {
            auto advance = std::to_underlying(lookupResult) >> 6;
            sourceOffset += advance;
            tok = Token(std::to_underlying(lookupResult) & (uint8_t)0b0011'1111);
        } else {
            switch (lookupResult) {
                using Result = decltype(lookupResult);
            case Result::Invalid: {
                char c0 = sourceBuffer[sourceOffset];
                if (c0 == '\0' && sourceOffset == (int_t)sourceBuffer.length()) {
                    this->tok = Token::EOS;
                    break;
                }
                HANDLE_LEXER_ACTION(errorHandler->invalidCharacter(this, c0));
            }
            case Result::NewlineLF:
            case Result::NewlineCRLF: {
                tok = Token::Newline;
                lineNumber += 1;
                sourceOffset += lookupResult == Result::NewlineCRLF ? 2 : 1;
                tokenStream.emit(StreamToken::makeNewline(lineNumber, sourceOffset));
                continue;
            }
            case Result::LineComment: {
                tok = Token::LineComment;
                skipToEndOfLine(sourceBuffer, sourceOffset);
                tokenStream.emit(StreamToken::make(Token::LineComment, tokBegin, sourceOffset));
                if (instrumenter)
                    instrumenter->handleComment(this);
                continue;
            }
            case Result::BlockComment: {
                tok = Token::BlockComment;
                skipToEndOfBlockComment(sourceBuffer, sourceOffset);
                if (sourceBuffer[sourceOffset] == '\0') [[unlikely]] {
                    HANDLE_LEXER_ACTION(errorHandler->unterminatedBlockComment(this, tokBegin));
                }
                sourceOffset += 2;
                tokenStream.emit(StreamToken::make(Token::BlockComment, tokBegin, sourceOffset));
                if (instrumenter)
                    instrumenter->handleComment(this);
                continue;
            }
            case Result::PunctuationPossiblyDoubleArrow: {
                if (sourceBuffer[sourceOffset + 2] == '>') {
                    tok = Token::DoubleArrow;
                    sourceOffset += 3;
                } else {
                    tok = Token::LessEqual;
                    sourceOffset += 2;
                }
                break;
            }
            case Result::Word: {
                tok = Token::Word;
                uint32_t hash = 0;
                do {
                    hash = Word::iterateHash(hash, sourceBuffer[sourceOffset]);
                    sourceOffset += 1;
                } while (isBulkWordCharacter(sourceBuffer[sourceOffset]));
                hash = Word::finalizeHash(hash);
                tokData = wordTable.getWithHash(source(tokBegin, sourceOffset), hash);
                break;
            }
            case Result::NumbericLiteral: {
                tok = Token::NumericLiteral;
                // TODO: implement parsing num literals
                for (;;) {
                    char c = sourceBuffer[sourceOffset];
                    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')
                        sourceOffset += 1;
                    else
                        break;
                }
                tokData = NumericLiteral();
                break;
            }
            case Result::CharacterLiteral: {
                tok = Token::CharacterLiteral;
                sourceOffset += 1;
                skipToEndOfCharacterLiteral(sourceBuffer, sourceOffset);
                if (sourceBuffer[sourceOffset] != '\'') [[unlikely]] {
                    HANDLE_LEXER_ACTION(errorHandler->unterminatedCharacterLiteral(this, tokBegin, sourceOffset));
                }
                int_t literalBegin = tokBegin + 1;
                int_t literalLength = sourceOffset - literalBegin;
                sourceOffset += 1;
                if (literalLength != 1 || (uint8_t)sourceBuffer[literalBegin] >= 0x80) [[unlikely]] {
                    HANDLE_LEXER_ACTION(errorHandler->invalidCharacterLiteral(this, literalBegin, literalBegin + literalLength));
                }
                tokData = CharacterLiteral { sourceBuffer[literalBegin] };
                break;
            }
            default:
                VERIFY_NOT_REACHED();
            }
        }
        break;
    }
    tokenStream.emit(StreamToken::make(tok, tokBegin, sourceOffset));
    if (instrumenter)
        instrumenter->nextToken(this);
}

void Lexer::reemitLastToken(TokenWithData token) {
    VERIFY(cachedNextToken.tok == Token::Invalid);
    cachedNextToken = *this;
    (TokenWithData&)* this = token;
}