#include <parse/Parser.h>
#include <parse/keyword_table.h>
#include <sema/Context.h>

#include <bit>
#include <bitset>
#include <concepts>
#include <random>
#include <utility>

#if CHARGE_SSE_OPTIMIZATIONS
#include <immintrin.h>
#endif

#include <gtest/gtest.h>

#ifdef __GNUC__
#define LABEL_MAYBE_UNUSED [[maybe_unused]]
#define NO_INLINE // [[gnu::noinline]]
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
[[maybe_unused]] static void checkLexToken(TokenKind semToken, LexerToken lexToken) {
    auto expected = lexerToken(semToken);
    VERIFY(expected == LexerToken::Invalid || lexToken == expected);
}

// Unused when SIMD optimizations are enabled
[[maybe_unused]] static constexpr bool isWordBulkCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

#if CHARGE_SSE_OPTIMIZATIONS
static unsigned nonWordCharacterMask(__m128i characters) {
    // A character is a word character iff loNibbleClasses[c & 0xf] & hiNibbleClasses[c >> 4] is
    // non zero. The classes are:
    //   bit 0: '0'-'9'   bit 1: '$'   bit 2: 'A'-'O' and 'a'-'o'
    //   bit 3: 'P'-'Z' and 'p'-'z'    bit 4: '_'
    //
    // Returns a bit per character of `characters`, set where it is not a word character.
    const __m128i loNibbleClasses = _mm_setr_epi8(
        0b01001, 0b01101, 0b01101, 0b01101, 0b01111, 0b01101, 0b01101, 0b01101,
        0b01101, 0b01101, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b10100);
    const __m128i hiNibbleClasses = _mm_setr_epi8(
        0, 0, 0b00010, 0b00001, 0b00100, 0b11000, 0b00100, 0b01000,
        0, 0, 0, 0, 0, 0, 0, 0);
    const __m128i lowNibbleMask = _mm_set1_epi8(0xf);
    __m128i lo = _mm_shuffle_epi8(loNibbleClasses, _mm_and_si128(characters, lowNibbleMask));
    __m128i hi = _mm_shuffle_epi8(hiNibbleClasses, _mm_and_si128(_mm_srli_epi16(characters, 4), lowNibbleMask));
    __m128i isWordCharacter = _mm_cmpeq_epi8(_mm_setzero_si128(), _mm_and_si128(lo, hi));
    return static_cast<unsigned>(_mm_movemask_epi8(isWordCharacter));
}

static __m128i loadWordBlock(const char* position) {
    static_assert(PADDED_STRING_PADDING >= 16);
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(position));
}

static const char* skipWordCharacters(const char* position) {
    while (true) {
        unsigned nonWordCharacters = nonWordCharacterMask(loadWordBlock(position));
        if (nonWordCharacters != 0)
            return position + std::countr_zero(nonWordCharacters);
        position += 16;
    }
}

static __m128i wordPrefixMask(unsigned length) {
    // 16 set bytes followed by 16 clear ones. Loading 16 bytes at offset `16 - n` gives a vector
    // whose first n bytes are set, which is how the tail block of a word is cut off at its end.
    alignas(16) static const unsigned char WORD_PREFIX_MASKS[32] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(WORD_PREFIX_MASKS + 16 - length));
}

static void iterateHash(WordHashState& state, __m128i block) {
    uint64_t low = static_cast<uint64_t>(_mm_cvtsi128_si64(block));
    uint64_t high = static_cast<uint64_t>(_mm_extract_epi64(block, 1));
    state.iterateHash(low, high);
}

struct ScannedWord {
    const char* end;
    uint32_t hash;
};

[[nodiscard]] [[gnu::noinline]] static ScannedWord scanAndHashLongWord(const char* begin, __m128i block) {
    WordHashState state;
    const char* position = begin;
    uint32_t terminatorMask;
    do {
        iterateHash(state, block);
        position += WordHashState::BLOCK_SIZE;
        block = loadWordBlock(position);
        terminatorMask = nonWordCharacterMask(block);
    } while (terminatorMask == 0);

    int_t tailSize = std::countr_zero(terminatorMask);
    iterateHash(state, _mm_and_si128(block, wordPrefixMask(tailSize)));
    return { position + tailSize, state.finalizeHash() };
}

// Scans the word starting at `begin` and hashes it out of the very vectors the scan loads.
[[nodiscard]] static ScannedWord scanAndHashWord(const char* begin) {
    __m128i block = loadWordBlock(begin);
    unsigned terminators = nonWordCharacterMask(block);
    if (terminators == 0) [[unlikely]]
        return scanAndHashLongWord(begin, block);

    int_t length = std::countr_zero(terminators);
    WordHashState state;
    iterateHash(state, _mm_and_si128(block, wordPrefixMask(length)));
    return { begin + length, state.finalizeHash() };
}

TEST(Parse, WordHashMatchesScan) {
    auto check = [](std::string_view word, std::string_view trailer) {
        padded_string buffer(std::string(word) + std::string(trailer));
        auto scanned = scanAndHashWord(buffer.data());
        EXPECT_EQ(scanned.end - buffer.data(), word.length()) << word << "|" << trailer;
        EXPECT_EQ(scanned.hash, Word::hash(word)) << word << "|" << trailer;
    };

    static constexpr std::string_view TRAILERS[] = { " ", "(", ";", "", "\n", ".x", "+y" };
    static constexpr std::string_view WORD_CHARACTERS =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$";

    for (std::string_view trailer : TRAILERS) {
        check("a", trailer);
        check("$", trailer);
        // Around and across the 16 byte block boundaries the scan works in.
        for (size_t length = 1; length <= 40; length++)
            check(std::string(length - 1, 'x') + "z", trailer);
    }

    std::mt19937 random(12345);
    std::uniform_int_distribution<size_t> lengths(1, 48);
    std::uniform_int_distribution<size_t> characters(0, WORD_CHARACTERS.size() - 1);
    for (int i = 0; i < 5000; i++) {
        std::string word;
        for (size_t length = lengths(random); word.size() < length;)
            word += WORD_CHARACTERS[characters(random)];
        check(word, TRAILERS[i % std::size(TRAILERS)]);
    }
}

TEST(Parse, WordHashOfKeywords) {
    words.forEachWord([](Word word, std::string_view text) {
        if (text.front() == '(') // generated placeholder names, never lexed
            return;
        padded_string buffer(text);
        auto scanned = scanAndHashWord(buffer.data());
        EXPECT_EQ(scanned.end - buffer.data(), text.length()) << text;
        EXPECT_EQ(scanned.hash, word.hash()) << text;
    });
}

#endif

static bool isWordFirstCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || c == '_' || c == '$' || c == '#';
}

template<std::same_as<LexerToken> T1>
constexpr uint32_t packData1(TokenKind, T1) {
    return 0;
}

static void setBackData1(sema::Context& context, auto data) {
    context.tokenBuffer.tokens.back().data1Bits = packData1(context.tokenBuffer.tokens.back().kind(), data);
}
static void setBackData2(sema::Context& context, auto data) {
    context.tokenBuffer.tokens.back().data2Bits = packData2(context.tokenBuffer.tokens.back().kind(), data);
}
static void setBackKind(sema::Context& context, TokenKind newKind) {
    // VERIFY(lexerToken(context.tokenBuffer.tokens.back().kind()) == lexerToken(newKind));
    context.tokenBuffer.tokens.back().setKind(newKind);
}

static void setBackData1(SimpleOutput&, auto) { }
static void setBackData2(SimpleOutput&, auto) { }
static void setBackKind(SimpleOutput& output, TokenKind newKind) {
    // VERIFY(lexerToken(output.tokenBuffer.tokens.back().kind()) == lexerToken(newKind));
    output.tokenBuffer.tokens.back().setKind(newKind);
}

static void setBackData1(const NoOutput&, auto) { }
static void setBackData2(const NoOutput&, auto) { }
static void setBackKind(const NoOutput&, TokenKind) { }

static ScopeKind* pushScope(ScopeKind* position, ScopeKind kind) {
    // dbgln("pushScope {}", nameString(kind));
    auto index = ScopeBuffer::toIndex(position);
    VERIFY(index + 1 < (size_t)SCOPE_BUFFER_SIZE);
    position += 1;
    position[0] = kind;
    return position;
}

template<typename... Args>
static ScopeKind* popScope(ScopeKind* position, Args... kinds) {
    // dbgln("popScope {}", nameString(*position));
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

static Word* addCallArgument(Word* ptr, LexerToken, SimpleOutput&) { return ptr; }
static void updateCallArgument(Word*, LexerToken, SimpleOutput&) { }
static Word* endCall(Word* ptr, SimpleOutput&) { return ptr; }
static Word* addCallArgument(Word* ptr, LexerToken, const NoOutput&) { return ptr; }
static void updateCallArgument(Word*, LexerToken, const NoOutput&) { }
static Word* endCall(Word* ptr, const NoOutput&) { return ptr; }

static SourceLocation locationInCurrentLine(const char* position, auto& tokenBuffer) {
    SourceLocation base = tokenBuffer.lastLineStartLocation;
    return {
        base.fileId(),
        base.lineIndex(),
        (uint32_t)(position - tokenBuffer.lastLineStartPosition)
    };
}

NO_INLINE static void emitToken(TokenKind kind, const char* begin, uint32_t data, sema::Context& output) {
    output.tokenBuffer.tokens.push_back({ kind, locationInCurrentLine(begin, output.tokenBuffer), data });
}

static void discardLastToken(sema::Context& output) {
    output.tokenBuffer.tokens.pop_back();
}

NO_INLINE static void emitToken(TokenKind kind, const char* begin, uint32_t, SimpleOutput& output) {
    output.tokenBuffer.tokens.push_back({ kind, locationInCurrentLine(begin, output.tokenBuffer) });
}

static void discardLastToken(SimpleOutput& output) {
    if (!output.tokenBuffer.tokens.empty())
        output.tokenBuffer.tokens.pop_back();
}

static void emitToken(TokenKind, const char*, uint32_t, const NoOutput&) { }
static void discardLastToken(const NoOutput&) { }

NO_INLINE static Word* emitCallToken(Word* argPos, TokenKind kind, const char* begin, sema::Context& output) {
    uint32_t tokenIndex = output.tokenBuffer.tokens.size();
    output.tokenBuffer.tokens.push_back({ kind, locationInCurrentLine(begin, output.tokenBuffer), 0 });

    auto index = ArgumentBuffer::toIndex(argPos);
    VERIFY(index + 2 < (size_t)ARGUMENT_BUFFER_SIZE);
    argPos[1] = Word::fromUint(tokenIndex);
    argPos[2] = Word::fromUint(0);
    argPos += 2;
    return argPos;
}
NO_INLINE static Word* emitCallToken(Word* ptr, TokenKind kind, const char* begin, SimpleOutput& output) {
    output.tokenBuffer.tokens.push_back({ kind, locationInCurrentLine(begin, output.tokenBuffer) });
    return ptr;
}
static Word* emitCallToken(Word* ptr, TokenKind, const char*, const NoOutput&) { return ptr; }

NO_INLINE static void markLineBegin(const char* position, sema::Context& output) {
    output.tokenBuffer.addLine(position);
}
static void markLineBegin(const char* position, SimpleOutput& output) {
    output.tokenBuffer.addLine(position);
}
static void markLineBegin(const char*, const NoOutput&) { }

struct WordAndPosition {
    const char* position;
    Word word;
};
[[nodiscard]] NO_INLINE static WordAndPosition readWord(const char* position, sema::Context& output) {
    const char* wordBegin = position;
#if CHARGE_SSE_OPTIMIZATIONS
    auto [end, hash] = scanAndHashWord(position);
    position = end;
#else
    do {
        position += 1;
    } while (isWordBulkCharacter(position[0]));
    uint32_t hash = Word::hash(std::string_view(wordBegin, position));
#endif
    Word word = output.tokenBuffer.wordTable.getWithHash(
        padded_string_view::from_raw_unsafe({ wordBegin, position }), hash);
    return { position, word };
}
static bool isIdentifierKeywordOrSpecial(Word word) {
    return word.id() < FIRST_REGULAR_IDENTIFIER_WORD_ID;
}
struct WordMask {
    int_t shiftBits;

    constexpr int_t resultBits() const { return 32 - shiftBits; }
    constexpr uint32_t mask(Word word) const { return word.toUint() >> shiftBits; }
};
constexpr WordMask keywordMask() {
    std::vector<Word> keywords;
    words.forEachWord([&](Word word, std::string_view) {
        if (!isKeyword(word) && !isSpecialIdentifier(word))
            return;
        keywords.push_back(word);
    });

    static constexpr int_t MAX_BITS = 16;
    for (int_t bits = 1; bits <= MAX_BITS; bits++) {
        std::bitset<(size_t)1 << MAX_BITS> set;
        int_t shiftBits = 32 - bits;
        bool allUnique = true;
        for (Word w : keywords) {
            uint32_t index = w.toUint() >> shiftBits;
            if (set.test(index)) {
                allUnique = false;
                break;
            }
            set.set(index);
        }
        if (allUnique)
            return { shiftBits };
    }
    VERIFY_NOT_REACHED();
}
inline constexpr auto KEYWORD_MASK = keywordMask();
constexpr uint32_t toSwitchValue(Word word) { return KEYWORD_MASK.mask(word); }
template<std::same_as<Word> T>
constexpr uint32_t toCaseValue(LexerToken, std::string_view str) {
    Word word = words.find(str);
    VERIFY(!word.empty());
    return toSwitchValue(word);
}

struct TokenAndPosition {
    const char* position;
    LexerToken word;
};
[[nodiscard]] NO_INLINE static TokenAndPosition readWord(const char* position, const NoOutput&) {
    const char* wordBegin = position;
#if CHARGE_SSE_OPTIMIZATIONS
    position = skipWordCharacters(position + 1);
#else
    do {
        position += 1;
    } while (isWordBulkCharacter(position[0]));
#endif
    const auto* entry = KeywordTable::get(wordBegin, position - wordBegin);
    if (entry == nullptr)
        return { position, LexerToken::Identifier };
    return { position, entry->token };
}
[[nodiscard]] static TokenAndPosition readWord(const char* position, SimpleOutput&) {
    return readWord(position, NoOutput());
}
static bool isIdentifierKeywordOrSpecial(LexerToken token) {
    return token != LexerToken::Identifier;
}
constexpr LexerToken toSwitchValue(LexerToken token) { return token; }
template<std::same_as<LexerToken> T>
constexpr LexerToken toCaseValue(LexerToken token, std::string_view) {
    return token;
}

[[nodiscard]] static const char* skipWhitespace(const char* position) {
    while (position[0] == ' ' || position[0] == '\t')
        position += 1;
    return position;
}

// advances offset to the next '*/'
[[nodiscard]] static const char* skipToEndOfBlockComment(const char* position) {
#if CHARGE_SSE_OPTIMIZATIONS
    static_assert(PADDED_STRING_PADDING >= 16);
    // The terminator is two characters, so pair up each '*' with the '/' one byte further on
    // by shifting the '/' comparison down through the vector. That drops the pair whose '*'
    // is the last character of the window, hence the stride of 15 instead of 16. Taking the
    // '/' from a second load at position + 1 keeps the full stride, but measures the same and
    // needs a readable byte past the window.
    const __m128i stars = _mm_set1_epi8('*');
    const __m128i slashes = _mm_set1_epi8('/');
    while (true) {
        __m128i characters = _mm_loadu_si128(reinterpret_cast<const __m128i*>(position));
        __m128i isSlash = _mm_cmpeq_epi8(characters, slashes);
        __m128i isTerminator = _mm_and_si128(_mm_cmpeq_epi8(characters, stars), _mm_srli_si128(isSlash, 1));
        __m128i ends = _mm_or_si128(_mm_cmpeq_epi8(characters, _mm_setzero_si128()), isTerminator);
        unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(ends));
        if (mask != 0)
            return position + std::countr_zero(mask);
        position += 15;
    }
#else
    while (position[0] != '\0' && !(position[0] == '*' && position[1] == '/')) {
        position += 1;
    }
    return position;
#endif
};

// advances offset to the next new line character
[[nodiscard]] static const char* skipToEndOfLine(const char* position) {
#if CHARGE_SSE_OPTIMIZATIONS
    static_assert(PADDED_STRING_PADDING >= 16);
    const __m128i newlines = _mm_set1_epi8('\n');
    const __m128i returns = _mm_set1_epi8('\r');
    while (true) {
        __m128i characters = _mm_loadu_si128(reinterpret_cast<const __m128i*>(position));
        __m128i ends = _mm_or_si128(_mm_cmpeq_epi8(characters, _mm_setzero_si128()),
            _mm_or_si128(_mm_cmpeq_epi8(characters, newlines), _mm_cmpeq_epi8(characters, returns)));
        unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(ends));
        if (mask != 0)
            return position + std::countr_zero(mask);
        position += 16;
    }
#else
    while (position[0] != '\0' && position[0] != '\n' && position[0] != '\r') {
        position += 1;
    }
    return position;
#endif
}

[[nodiscard]] static const char* skipToEndOfCharacterLiteral(const char* position) {
    position += 1;
    while (position[0] != '\0' && position[0] != '\'' && position[0] != '\n' && position[0] != '\r') {
        position += 1;
    }
    return position;
};

static constexpr bool isStringLiteralCharacter(uint8_t c) {
    return c >= 0x20 && c < 0x7f;
}

[[nodiscard]] static const char* skipToEndOfStringLiteral(const char* position) {
    position += 1;
    while (isStringLiteralCharacter(position[0])) {
        if (position[0] == '"')
            return position;
        // A backslash escapes the next character, so \" does not terminate the literal
        if (position[0] == '\\' && isStringLiteralCharacter(position[1]))
            position += 1;
        position += 1;
    }
    return position;
};

NO_INLINE static void emitWhitespace(WhitespaceKind kind, const char* begin, const char* end, sema::Context& output) {
    output.tokenBuffer.whitespace.push_back({ { kind, locationInCurrentLine(begin, output.tokenBuffer) }, (uint32_t)(end - begin) });
}
static void emitWhitespace(WhitespaceKind kind, const char* begin, const char* end, SimpleOutput& output) {
    output.tokenBuffer.whitespace.push_back({ { kind, locationInCurrentLine(begin, output.tokenBuffer) }, (uint32_t)(end - begin) });
}
static void emitWhitespace(WhitespaceKind, const char*, const char*, const NoOutput&) { }

template<typename ParseOutput>
[[nodiscard]] NO_INLINE static const char* inlineAdvancer(const char* tokEnd, ParseOutput& output) {
    for (;;) {
        tokEnd = skipWhitespace(tokEnd);
        const char* tokBegin = tokEnd;
        if (std::string_view(tokEnd, 2) == "//") {
            tokEnd = skipToEndOfLine(tokEnd + 2);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            continue;
        }
        if (std::string_view(tokEnd, 2) == "/*") {
            tokEnd = skipToEndOfBlockComment(tokEnd + 2);
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
    // dbgln("commitDeclaration {}", output.tokenBuffer.wordTable.view(name));
    if constexpr (kind == DeclarationKind::Member || kind == DeclarationKind::BaseMember) {
        return output.pushMemberScope(kind == DeclarationKind::BaseMember, name, declarationBegin, locationInCurrentLine(currentPosition, output.tokenBuffer));
    } else if constexpr (kind == DeclarationKind::EnumValue) {
        return output.pushEnumValueScope(name, declarationBegin, locationInCurrentLine(currentPosition, output.tokenBuffer));
    } else if constexpr (kind == DeclarationKind::Namespace) {
        return output.pushNamespaceScope(name);
    } else {
        return output.pushStaticScope(programKindForDeclaration(kind), name, declarationBegin, locationInCurrentLine(currentPosition, output.tokenBuffer));
    }
}

template<DeclarationKind kind>
static sema::DeclarationValue commitImplDeclaration(const char* currentPosition, TokenHandle declarationBegin, sema::Context& output) {
    static_assert(kind == DeclarationKind::Struct || kind == DeclarationKind::Function || kind == DeclarationKind::Enum || kind == DeclarationKind::StaticVariable);
    return output.pushStaticImplScope(programKindForDeclaration(kind), declarationBegin, locationInCurrentLine(currentPosition, output.tokenBuffer));
}

static void endDeclaration(sema::Context& output) {
    // dbgln("endDeclaration on line {}", output.tokenBuffer.lines.size());
    output.popScope(output.tokenBuffer.currentToken());
}

using GlobalKind = sema::GlobalKind;
static void setGlobalKind(sema::Context& output, GlobalKind kind) {
    sema::cast<sema::GlobalProgram>(output.currentProgram())->m_globalKind = kind;
}

template<DeclarationKind kind>
static sema::DeclarationValue commitDeclaration(LexerToken, const char*, TokenHandle, SimpleOutput&) {
    return sema::INVALID_DECLARATION_VALUE;
}
template<DeclarationKind kind>
static sema::DeclarationValue commitImplDeclaration(const char*, TokenHandle, SimpleOutput&) {
    return sema::INVALID_DECLARATION_VALUE;
}

template<DeclarationKind kind>
static sema::DeclarationValue commitDeclaration(LexerToken, const char*, TokenHandle, const NoOutput&) {
    return sema::INVALID_DECLARATION_VALUE;
}
template<DeclarationKind kind>
static sema::DeclarationValue commitImplDeclaration(const char*, TokenHandle, const NoOutput&) {
    return sema::INVALID_DECLARATION_VALUE;
}

static void endDeclaration(SimpleOutput&) { }
static void setGlobalKind(SimpleOutput&, GlobalKind) { }
static void endDeclaration(const NoOutput&) { }
static void setGlobalKind(const NoOutput&, GlobalKind) { }

template<typename ParseOutput>
LexerToken lexImpl(char const*& tokEnd, ParseOutput& output) {
    const char* tokBegin = tokEnd;
    using identifier_t = decltype(readWord(nullptr, output).word);
    identifier_t this_identifier;

lex$retry:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, output);
        goto lex$retry;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, output);
        goto lex$retry;
    }
    case '!': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::ExclaimEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Exclaim;
    }
    case '%': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::PercentEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Percent;
    }
    case '&': {
        if (tokEnd[1] == '&') {
            if (tokEnd[2] == '=') {
                tokEnd += 3;
                // lexToken
                return LexerToken::AmpAmpEqual;
            }
            tokEnd += 2;
            // lexToken
            return LexerToken::AmpAmp;
        }
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::StarEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Star;
    }
    case '+': {
        if (tokEnd[1] == '+') {
            tokEnd += 2;
            // lexToken
            return LexerToken::PlusPlus;
        }
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == '-') {
            tokEnd += 2;
            // lexToken
            return LexerToken::MinusMinus;
        }
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::MinusEqual;
        }
        if (tokEnd[1] == '>') {
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
        if (tokEnd[1] == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            goto lex$retry;
        }
        if (tokEnd[1] == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            goto lex$retry;
        }
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::SlashEqual;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Slash;
    }
    case ':': {
        if (tokEnd[1] == ':') {
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
        if (tokEnd[1] == '<') {
            if (tokEnd[2] == '=') {
                tokEnd += 3;
                // lexToken
                return LexerToken::LessLessEqual;
            }
            tokEnd += 2;
            // lexToken
            return LexerToken::LessLess;
        }
        if (tokEnd[1] == '=') {
            if (tokEnd[2] == '>') {
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
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::EqualEqual;
        }
        if (tokEnd[1] == '>') {
            tokEnd += 2;
            // lexToken
            return LexerToken::EqualGreater;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::Equal;
    }
    case '>': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::GreaterEqual;
        }
        if (tokEnd[1] == '>') {
            if (tokEnd[2] == '=') {
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
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // lexToken
            return LexerToken::VertEqual;
        }
        if (tokEnd[1] == '|') {
            if (tokEnd[2] == '=') {
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
        return LexerToken::NumericLiteral;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // lexToken
        return LexerToken::CharacterLiteral;
    }
    case '"': {
        tokEnd = skipToEndOfStringLiteral(tokEnd);
        if (tokEnd[0] != '"') {
            goto error$as_then;
        }
        tokEnd += 1;
        // lexToken
        return LexerToken::StringLiteral;
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
        goto lex$word_case_with_read;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
LABEL_MAYBE_UNUSED lex$word_case_with_read:
    {
        auto wordAndPos = readWord(tokEnd, output);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
LABEL_MAYBE_UNUSED lex$word_case:
    if (isIdentifierKeywordOrSpecial(this_identifier)) {
        switch (toSwitchValue(this_identifier)) {
        case toCaseValue<identifier_t>(LexerToken::Assert, "assert"):
            // lexToken
            return LexerToken::Assert;
        case toCaseValue<identifier_t>(LexerToken::Base, "base"):
            // lexToken
            return LexerToken::Base;
        case toCaseValue<identifier_t>(LexerToken::Break, "break"):
            // lexToken
            return LexerToken::Break;
        case toCaseValue<identifier_t>(LexerToken::Catch, "catch"):
            // lexToken
            return LexerToken::Catch;
        case toCaseValue<identifier_t>(LexerToken::Const, "const"):
            // lexToken
            return LexerToken::Const;
        case toCaseValue<identifier_t>(LexerToken::Context, "context"):
            // lexToken
            return LexerToken::Context;
        case toCaseValue<identifier_t>(LexerToken::Continue, "continue"):
            // lexToken
            return LexerToken::Continue;
        case toCaseValue<identifier_t>(LexerToken::Destroy, "destroy"):
            // lexToken
            return LexerToken::Destroy;
        case toCaseValue<identifier_t>(LexerToken::Discard, "discard"):
            // lexToken
            return LexerToken::Discard;
        case toCaseValue<identifier_t>(LexerToken::Do, "do"):
            // lexToken
            return LexerToken::Do;
        case toCaseValue<identifier_t>(LexerToken::Elif, "elif"):
            // lexToken
            return LexerToken::Elif;
        case toCaseValue<identifier_t>(LexerToken::Else, "else"):
            // lexToken
            return LexerToken::Else;
        case toCaseValue<identifier_t>(LexerToken::Enum, "enum"):
            // lexToken
            return LexerToken::Enum;
        case toCaseValue<identifier_t>(LexerToken::Fn, "fn"):
            // lexToken
            return LexerToken::Fn;
        case toCaseValue<identifier_t>(LexerToken::For, "for"):
            // lexToken
            return LexerToken::For;
        case toCaseValue<identifier_t>(LexerToken::If, "if"):
            // lexToken
            return LexerToken::If;
        case toCaseValue<identifier_t>(LexerToken::Impl, "impl"):
            // lexToken
            return LexerToken::Impl;
        case toCaseValue<identifier_t>(LexerToken::In, "in"):
            // lexToken
            return LexerToken::In;
        case toCaseValue<identifier_t>(LexerToken::Incomplete, "incomplete"):
            // lexToken
            return LexerToken::Incomplete;
        case toCaseValue<identifier_t>(LexerToken::Let, "let"):
            // lexToken
            return LexerToken::Let;
        case toCaseValue<identifier_t>(LexerToken::Loop, "loop"):
            // lexToken
            return LexerToken::Loop;
        case toCaseValue<identifier_t>(LexerToken::Namespace, "namespace"):
            // lexToken
            return LexerToken::Namespace;
        case toCaseValue<identifier_t>(LexerToken::Open, "open"):
            // lexToken
            return LexerToken::Open;
        case toCaseValue<identifier_t>(LexerToken::Prove, "prove"):
            // lexToken
            return LexerToken::Prove;
        case toCaseValue<identifier_t>(LexerToken::Return, "return"):
            // lexToken
            return LexerToken::Return;
        case toCaseValue<identifier_t>(LexerToken::Shared, "shared"):
            // lexToken
            return LexerToken::Shared;
        case toCaseValue<identifier_t>(LexerToken::Static, "static"):
            // lexToken
            return LexerToken::Static;
        case toCaseValue<identifier_t>(LexerToken::Struct, "struct"):
            // lexToken
            return LexerToken::Struct;
        case toCaseValue<identifier_t>(LexerToken::Template, "template"):
            // lexToken
            return LexerToken::Template;
        case toCaseValue<identifier_t>(LexerToken::Trait, "trait"):
            // lexToken
            return LexerToken::Trait;
        case toCaseValue<identifier_t>(LexerToken::Try, "try"):
            // lexToken
            return LexerToken::Try;
        case toCaseValue<identifier_t>(LexerToken::Unique, "unique"):
            // lexToken
            return LexerToken::Unique;
        case toCaseValue<identifier_t>(LexerToken::Var, "var"):
            // lexToken
            return LexerToken::Var;
        case toCaseValue<identifier_t>(LexerToken::Virtual, "virtual"):
            // lexToken
            return LexerToken::Virtual;
        case toCaseValue<identifier_t>(LexerToken::While, "while"):
            // lexToken
            return LexerToken::While;
        default:
            if (isKeyword(this_identifier)) {
                goto error$as_then;
            }
            break;
        }
    }
    // lexToken
    return LexerToken::Identifier;

error$as_then:
    VERIFY_NOT_REACHED();
}

template<typename ParseOutput>
[[gnu::always_inline]] inline Parser::InternalState Parser::parseImpl(const Parser::InternalState& inState, ParseOutput& output, int_t tokenLimit) {
    State parseState = inState.state;
    const char* tokBegin = inState.sourcePosition;
    const char* tokEnd = inState.sourcePosition;
    ScopeKind* scopePosition = inState.scopePosition;
    Word* argumentPosition = inState.argumentPosition;
    TokenKind carriedEmitTokenKind = (TokenKind)0;
    uint32_t carriedEmitTokenData = 0;
    using identifier_t = decltype(readWord(nullptr, output).word);
    identifier_t this_identifier;
    sema::DeclarationValue this_declaration = sema::INVALID_DECLARATION_VALUE;
    TokenHandle declarationBegin = inState.declarationBegin;
    identifier_t argumentName = {};
    if constexpr (std::is_same_v<identifier_t, Word>)
        argumentName = inState.savedArgumentName;
    int_t expectedParsedTokens = (int_t)inState.parsedTokens + tokenLimit;

    switch (parseState) {
    case State::Start:
        goto start$no_emit;
    case State::Expression:
        goto expression$no_emit;
    case State::AfterExpression:
        goto after_expression$no_emit;
    case State::CommaAfterExpressionInArguments:
        goto comma_after_expression_in_arguments$no_emit;
    case State::CommaAfterExpressionInParameters:
        goto comma_after_expression_in_parameters$no_emit;
    case State::CommaElse:
        goto comma_else$no_emit;
    case State::Argument:
        goto argument$no_emit;
    case State::CheckDesignatedArgument:
        VERIFY_NOT_REACHED();
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
    case State::CheckElseBranch:
        goto check_else_branch$no_emit;
    case State::ElseBranch:
        goto else_branch$no_emit;
    case State::DoBody:
        goto do_body$no_emit;
    case State::AfterDoBody:
        goto after_do_body$no_emit;
    case State::LoopBody:
        goto loop_body$no_emit;
    case State::ForVariable:
        goto for_variable$no_emit;
    case State::AfterLoopControl:
        goto after_loop_control$no_emit;
    case State::AfterSimpleVariableDeclarationId:
        goto after_simple_variable_declaration_id$no_emit;
    case State::AfterVariableDeclarationId:
        goto after_variable_declaration_id$no_emit;
    case State::VariableType:
        goto variable_type$no_emit;
    case State::ReferenceModifier:
        goto reference_modifier$no_emit;
    case State::AfterReferenceModifier:
        goto after_reference_modifier$no_emit;
    case State::AfterVariableModifier:
        VERIFY_NOT_REACHED();
    case State::AfterVariableUniqueModifier:
        goto after_variable_unique_modifier$no_emit;
    case State::AfterVariableSharedModifier:
        goto after_variable_shared_modifier$no_emit;
    case State::AfterVariableConstModifier:
        goto after_variable_const_modifier$no_emit;
    case State::ReturnType:
        goto return_type$no_emit;
    case State::AfterReturnTypeModifier:
        VERIFY_NOT_REACHED();
    case State::Borrow:
        goto borrow$no_emit;
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
    default:
        VERIFY_NOT_REACHED();
    }
    // LinearState start
start$no_emit:
    // pushScope ScopeKind::Start
    scopePosition = pushScope(scopePosition, ScopeKind::Start);
    // pushScope ScopeKind::Namespace
    scopePosition = pushScope(scopePosition, ScopeKind::Namespace);
    // next namespace_declaration
    goto namespace_declaration$no_emit;

    // SwitchState expression
expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
expression$no_emit:
    parseState = State::Expression;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
expression$retry:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED expression$as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, output);
        goto expression$retry;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, output);
        goto expression$retry;
    }
    case '!': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::LogicalNotExpr
        carriedEmitTokenKind = TokenKind::LogicalNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '%': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '&': {
        if (tokEnd[1] == '&') {
            if (tokEnd[2] == '=') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::DereferenceExpr
        carriedEmitTokenKind = TokenKind::DereferenceExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        if (tokEnd[1] == '+') {
            tokEnd += 2;
            // emitToken TokenKind::PreIncrementExpr
            carriedEmitTokenKind = TokenKind::PreIncrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::PlusExpr
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
        if (tokEnd[1] == '-') {
            tokEnd += 2;
            // emitToken TokenKind::PreDecrementExpr
            carriedEmitTokenKind = TokenKind::PreDecrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '>') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::NegateExpr
        carriedEmitTokenKind = TokenKind::NegateExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '.': {
        tokEnd += 1;
        // emitToken TokenKind::ImplicitSelfReference
        carriedEmitTokenKind = TokenKind::ImplicitSelfReference;
        carriedEmitTokenData = 0;
        // next member_access
        goto member_access$with_emit;
    }
    case '/': {
        if (tokEnd[1] == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            goto expression$retry;
        }
        if (tokEnd[1] == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            goto expression$retry;
        }
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case ':': {
        if (tokEnd[1] == ':') {
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
        if (tokEnd[1] == '<') {
            if (tokEnd[2] == '=') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '=') {
            if (tokEnd[2] == '>') {
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
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '>') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // -> error
        goto error$as_then;
    }
    case '>': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '>') {
            if (tokEnd[2] == '=') {
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
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '|') {
            if (tokEnd[2] == '=') {
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
        // emitToken TokenKind::NumericLiteralExpr
        carriedEmitTokenKind = TokenKind::NumericLiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '\'': {
        tokEnd = skipToEndOfCharacterLiteral(tokEnd);
        VERIFY(tokEnd[0] == '\'');
        tokEnd += 1;
        // emitToken TokenKind::CharacterLiteralExpr
        carriedEmitTokenKind = TokenKind::CharacterLiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '"': {
        tokEnd = skipToEndOfStringLiteral(tokEnd);
        if (tokEnd[0] != '"') {
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::StringLiteralExpr
        carriedEmitTokenKind = TokenKind::StringLiteralExpr;
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
        goto expression$word_case_with_read;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
LABEL_MAYBE_UNUSED expression$word_case_with_read:
    {
        auto wordAndPos = readWord(tokEnd, output);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
LABEL_MAYBE_UNUSED expression$word_case:
    if (isIdentifierKeywordOrSpecial(this_identifier)) {
        switch (toSwitchValue(this_identifier)) {
        case toCaseValue<identifier_t>(LexerToken::If, "if"):
            // pushScope ScopeKind::IfExpr
            scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
            // next expression
            goto expression$no_emit;
        default:
            if (isKeyword(this_identifier)) {
                goto error$as_then;
            }
            break;
        }
    }
    // emitToken TokenKind::IdentifierExpr
    carriedEmitTokenKind = TokenKind::IdentifierExpr;
    carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
    // next after_expression
    goto after_expression$with_emit;

    // SwitchState after_expression
after_expression$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_expression$no_emit:
    parseState = State::AfterExpression;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
after_expression$retry:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_expression$as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, output);
        goto after_expression$retry;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, output);
        goto after_expression$retry;
    }
    case '!': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareNotEqualExpr
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
        if (tokEnd[1] == '=') {
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
            carriedEmitTokenKind = TokenKind::RemainderUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::RemainderExpr
        carriedEmitTokenKind = TokenKind::RemainderExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '&': {
        if (tokEnd[1] == '&') {
            if (tokEnd[2] == '=') {
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
                carriedEmitTokenKind = TokenKind::LogicalAndUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::LogicalAndExpr
            carriedEmitTokenKind = TokenKind::LogicalAndExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '=') {
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
            carriedEmitTokenKind = TokenKind::BitwiseAndUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseAndExpr
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
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next after_impl_expression
            goto after_impl_expression$with_emit;
        }
        // ifScope ScopeKind::GenericCategoryExpression
        if (scopePosition[0] == ScopeKind::GenericCategoryExpression) {
            // popScope ScopeKind::GenericCategoryExpression
            {
                auto result = popScope(scopePosition, ScopeKind::GenericCategoryExpression);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // next after_reference_modifier
            goto after_reference_modifier$no_emit;
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
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '*': {
        if (tokEnd[1] == '=') {
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
            carriedEmitTokenKind = TokenKind::MultiplyUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::MultiplyExpr
        carriedEmitTokenKind = TokenKind::MultiplyExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        if (tokEnd[1] == '+') {
            tokEnd += 2;
            // emitToken TokenKind::PostIncrementExpr
            carriedEmitTokenKind = TokenKind::PostIncrementExpr;
            carriedEmitTokenData = 0;
            // next after_expression
            goto after_expression$with_emit;
        }
        if (tokEnd[1] == '=') {
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
            carriedEmitTokenKind = TokenKind::AdditionUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::AdditionExpr
        carriedEmitTokenKind = TokenKind::AdditionExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ',': {
        tokEnd += 1;
        // ifScope ScopeKind::BorrowList
        if (scopePosition[0] == ScopeKind::BorrowList) {
            // next borrow
            goto borrow$no_emit;
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
            carriedEmitTokenKind = TokenKind::ExpressionStmt;
            carriedEmitTokenData = 0;
            // next comma_after_expression_in_parameters
            goto comma_after_expression_in_parameters$with_emit;
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
            carriedEmitTokenKind = TokenKind::ExpressionStmt;
            carriedEmitTokenData = 0;
            // next comma_after_expression_in_parameters
            goto comma_after_expression_in_parameters$with_emit;
        }
        // next comma_after_expression_in_arguments
        goto comma_after_expression_in_arguments$no_emit;
    }
    case '-': {
        if (tokEnd[1] == '-') {
            tokEnd += 2;
            // emitToken TokenKind::PostDecrementExpr
            carriedEmitTokenKind = TokenKind::PostDecrementExpr;
            carriedEmitTokenData = 0;
            // next after_expression
            goto after_expression$with_emit;
        }
        if (tokEnd[1] == '=') {
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
            carriedEmitTokenKind = TokenKind::SubtractionUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '>') {
            tokEnd += 2;
            // -> error
            goto error$as_then;
        }
        tokEnd += 1;
        // emitToken TokenKind::SubtractionExpr
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
        if (tokEnd[1] == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            goto after_expression$retry;
        }
        if (tokEnd[1] == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            goto after_expression$retry;
        }
        if (tokEnd[1] == '=') {
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
            carriedEmitTokenKind = TokenKind::DivideUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::DivideExpr
        carriedEmitTokenKind = TokenKind::DivideExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case ':': {
        if (tokEnd[1] == ':') {
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
            carriedEmitTokenKind = TokenKind::FunctionBody;
            carriedEmitTokenData = 0;
            // next statement
            goto statement$with_emit;
        }
        // ifScope ScopeKind::IfExprOrStmt
        if (scopePosition[0] == ScopeKind::IfExprOrStmt) {
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
            carriedEmitTokenKind = TokenKind::IfStmt;
            carriedEmitTokenData = 0;
            // next statement
            goto statement$with_emit;
        }
        // ifScope ScopeKind::WhileCondition
        if (scopePosition[0] == ScopeKind::WhileCondition) {
            // popScope ScopeKind::WhileCondition
            {
                auto result = popScope(scopePosition, ScopeKind::WhileCondition);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::LoopBody
            scopePosition = pushScope(scopePosition, ScopeKind::LoopBody);
            // emitToken TokenKind::WhileBody
            carriedEmitTokenKind = TokenKind::WhileBody;
            carriedEmitTokenData = 0;
            // next statement
            goto statement$with_emit;
        }
        // ifScope ScopeKind::ForRange
        if (scopePosition[0] == ScopeKind::ForRange) {
            // popScope ScopeKind::ForRange
            {
                auto result = popScope(scopePosition, ScopeKind::ForRange);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::LoopBody
            scopePosition = pushScope(scopePosition, ScopeKind::LoopBody);
            // emitToken TokenKind::ForBody
            carriedEmitTokenKind = TokenKind::ForBody;
            carriedEmitTokenData = 0;
            // next statement
            goto statement$with_emit;
        }
        // popScope ScopeKind::LeftExpr
        {
            auto result = popScope(scopePosition, ScopeKind::LeftExpr);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // pushScope ScopeKind::ContextFunction
        scopePosition = pushScope(scopePosition, ScopeKind::ContextFunction);
        // emitToken TokenKind::ContextFunctionStmt
        carriedEmitTokenKind = TokenKind::ContextFunctionStmt;
        carriedEmitTokenData = 0;
        // next statement
        goto statement$with_emit;
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
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next after_declaration
            goto after_declaration$with_emit;
        }
        // ifScope ScopeKind::DoWhileCondition
        if (scopePosition[0] == ScopeKind::DoWhileCondition) {
            // popScope ScopeKind::DoWhileCondition
            {
                auto result = popScope(scopePosition, ScopeKind::DoWhileCondition);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::ExpressionStmt
            carriedEmitTokenKind = TokenKind::ExpressionStmt;
            carriedEmitTokenData = 0;
            // next after_statement
            goto after_statement$with_emit;
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
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_statement
        goto after_statement$with_emit;
    }
    case '<': {
        if (tokEnd[1] == '<') {
            if (tokEnd[2] == '=') {
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
                carriedEmitTokenKind = TokenKind::ShiftLeftUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::ShiftLeftExpr
            carriedEmitTokenKind = TokenKind::ShiftLeftExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '=') {
            if (tokEnd[2] == '>') {
                tokEnd += 3;
                // -> error
                goto error$as_then;
            }
            tokEnd += 2;
            // emitToken TokenKind::CompareLessEqualExpr
            carriedEmitTokenKind = TokenKind::CompareLessEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::CompareLessExpr
        carriedEmitTokenKind = TokenKind::CompareLessExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '=': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareEqualExpr
            carriedEmitTokenKind = TokenKind::CompareEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '>') {
            tokEnd += 2;
            // ifScope ScopeKind::IfExpr, ScopeKind::IfExprOrStmt
            if (scopePosition[0] == ScopeKind::IfExpr || scopePosition[0] == ScopeKind::IfExprOrStmt) {
                // popScope ScopeKind::IfExpr, ScopeKind::IfExprOrStmt
                {
                    auto result = popScope(scopePosition, ScopeKind::IfExpr, ScopeKind::IfExprOrStmt);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // emitToken TokenKind::IfExpr
                carriedEmitTokenKind = TokenKind::IfExpr;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
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
            // emitToken TokenKind::ContextExpressionStmt
            carriedEmitTokenKind = TokenKind::ContextExpressionStmt;
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
        carriedEmitTokenKind = TokenKind::AssignStmt;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '>': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // emitToken TokenKind::CompareGreaterEqualExpr
            carriedEmitTokenKind = TokenKind::CompareGreaterEqualExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '>') {
            if (tokEnd[2] == '=') {
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
                carriedEmitTokenKind = TokenKind::ShiftRightUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::ShiftRightExpr
            carriedEmitTokenKind = TokenKind::ShiftRightExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::CompareGreaterExpr
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
        // ifScope ScopeKind::BorrowList
        if (scopePosition[0] == ScopeKind::BorrowList) {
            // popScope ScopeKind::BorrowList
            {
                auto result = popScope(scopePosition, ScopeKind::BorrowList);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // emitToken TokenKind::EmptyNode
            carriedEmitTokenKind = TokenKind::EmptyNode;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
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
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '^': {
        if (tokEnd[1] == '=') {
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
            carriedEmitTokenKind = TokenKind::BitwiseXorUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseXorExpr
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
        if (tokEnd[1] == '=') {
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
            carriedEmitTokenKind = TokenKind::BitwiseOrUpdateStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '|') {
            if (tokEnd[2] == '=') {
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
                carriedEmitTokenKind = TokenKind::LogicalOrUpdateStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            }
            tokEnd += 2;
            // emitToken TokenKind::LogicalOrExpr
            carriedEmitTokenKind = TokenKind::LogicalOrExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        tokEnd += 1;
        // emitToken TokenKind::BitwiseOrExpr
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
    case '"': {
        tokEnd = skipToEndOfStringLiteral(tokEnd);
        if (tokEnd[0] != '"') {
            goto error$as_then;
        }
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
        goto after_expression$word_case_with_read;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
LABEL_MAYBE_UNUSED after_expression$word_case_with_read:
    {
        auto wordAndPos = readWord(tokEnd, output);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
LABEL_MAYBE_UNUSED after_expression$word_case:
    if (isIdentifierKeywordOrSpecial(this_identifier)) {
        switch (toSwitchValue(this_identifier)) {
        case toCaseValue<identifier_t>(LexerToken::In, "in"):
            // popScope ScopeKind::VariableType
            {
                auto result = popScope(scopePosition, ScopeKind::VariableType);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // popScope ScopeKind::ForVariable
            {
                auto result = popScope(scopePosition, ScopeKind::ForVariable);
                if (result == nullptr) {
                    goto pop_scope_failed;
                }
                scopePosition = result;
            }
            // pushScope ScopeKind::ForRange
            scopePosition = pushScope(scopePosition, ScopeKind::ForRange);
            // emitToken TokenKind::ForIn
            carriedEmitTokenKind = TokenKind::ForIn;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        default:
            if (isKeyword(this_identifier)) {
                goto error$as_then;
            }
            break;
        }
    }
    // -> error
    goto error$as_then;

    // LinearState comma_after_expression_in_arguments
comma_after_expression_in_arguments$no_emit:
    parseState = State::CommaAfterExpressionInArguments;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED comma_after_expression_in_arguments$as_then:
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
        carriedEmitTokenKind = TokenKind::EmptyNode;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED comma_after_expression_in_arguments$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED comma_after_expression_in_arguments$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Else, "else"):
                // next comma_else
                goto comma_else$no_emit;
            case toCaseValue<identifier_t>(LexerToken::If, "if"):
                // -> argument
                // callArgument
                argumentPosition = addCallArgument(argumentPosition, identifier_t(), output);
                // emitToken TokenKind::CallArgument
                emitToken(TokenKind::CallArgument, tokBegin, 0, output);
                // -> check_designated_argument
                // -> expression
                // pushScope ScopeKind::IfExpr
                scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                // next expression
                goto expression$no_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> argument
        // callArgument
        argumentPosition = addCallArgument(argumentPosition, identifier_t(), output);
        // emitToken TokenKind::CallArgument
        emitToken(TokenKind::CallArgument, tokBegin, 0, output);
        // -> check_designated_argument
        // argumentName = this_identifier
        argumentName = this_identifier;
        // emitToken TokenKind::IdentifierExpr
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next maybe_designated_argument
        goto maybe_designated_argument$with_emit;
    }
    // then argument
    goto argument$as_then;

    // LinearState comma_after_expression_in_parameters
comma_after_expression_in_parameters$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
comma_after_expression_in_parameters$no_emit:
    parseState = State::CommaAfterExpressionInParameters;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED comma_after_expression_in_parameters$as_then:
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
        // next after_parameters
        goto after_parameters$no_emit;
    }
    // then parameter
    goto parameter$as_then;

    // LinearState comma_else
comma_else$no_emit:
    parseState = State::CommaElse;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED comma_else$as_then:
    if (std::string_view(tokEnd, 2) == "=>"sv) {
        tokEnd += 2;
        // emitToken TokenKind::CommaElseExpr
        carriedEmitTokenKind = TokenKind::CommaElseExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState argument
argument$no_emit:
    parseState = State::Argument;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED argument$as_then:
    // callArgument
    argumentPosition = addCallArgument(argumentPosition, identifier_t(), output);
    // emitToken TokenKind::CallArgument
    emitToken(TokenKind::CallArgument, tokBegin, 0, output);
    // then check_designated_argument
    goto check_designated_argument$as_then;

    // LinearState check_designated_argument
LABEL_MAYBE_UNUSED check_designated_argument$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED check_designated_argument$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED check_designated_argument$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::If, "if"):
                // -> expression
                // pushScope ScopeKind::IfExpr
                scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                // next expression
                goto expression$no_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // argumentName = this_identifier
        argumentName = this_identifier;
        // emitToken TokenKind::IdentifierExpr
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
    parseState = State::MaybeDesignatedArgument;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED maybe_designated_argument$as_then:
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
    parseState = State::FirstArgumentParen;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED first_argument_paren$as_then:
    if (std::string_view(tokEnd, 1) == ")"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
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
    parseState = State::FirstArgumentSquare;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED first_argument_square$as_then:
    if (std::string_view(tokEnd, 1) == "]"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
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
    parseState = State::FirstArgumentBrace;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED first_argument_brace$as_then:
    if (std::string_view(tokEnd, 1) == "}"sv) {
        tokEnd += 1;
        // endCall
        argumentPosition = endCall(argumentPosition, output);
        // emitToken TokenKind::EmptyNode
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
    parseState = State::MemberAccess;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED member_access$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED member_access$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED member_access$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::MemberAccessExpr
        carriedEmitTokenKind = TokenKind::MemberAccessExpr;
        carriedEmitTokenData = packData1(TokenKind::MemberAccessExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_access
static_access$no_emit:
    parseState = State::StaticAccess;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED static_access$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED static_access$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED static_access$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::StaticAccessExpr
        carriedEmitTokenKind = TokenKind::StaticAccessExpr;
        carriedEmitTokenData = packData1(TokenKind::StaticAccessExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_statement
after_statement$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_statement$no_emit:
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
        // next after_declaration
        goto after_declaration$no_emit;
    }
    // ifScope ScopeKind::Struct, ScopeKind::Namespace, ScopeKind::Enum
    if (scopePosition[0] == ScopeKind::Struct || scopePosition[0] == ScopeKind::Namespace || scopePosition[0] == ScopeKind::Enum) {
        // next after_declaration
        goto after_declaration$no_emit;
    }
    // ifScope ScopeKind::IfBranch
    if (scopePosition[0] == ScopeKind::IfBranch) {
        // popScope ScopeKind::IfBranch
        {
            auto result = popScope(scopePosition, ScopeKind::IfBranch);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // next check_else_branch
        goto check_else_branch$no_emit;
    }
    // ifScope ScopeKind::ElseBranch
    if (scopePosition[0] == ScopeKind::ElseBranch) {
        // popScope ScopeKind::ElseBranch
        {
            auto result = popScope(scopePosition, ScopeKind::ElseBranch);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // next statement
        goto statement$no_emit;
    }
    // ifScope ScopeKind::LoopBody
    if (scopePosition[0] == ScopeKind::LoopBody) {
        // popScope ScopeKind::LoopBody
        {
            auto result = popScope(scopePosition, ScopeKind::LoopBody);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // next after_statement
        goto after_statement$no_emit;
    }
    // ifScope ScopeKind::DoBody
    if (scopePosition[0] == ScopeKind::DoBody) {
        // popScope ScopeKind::DoBody
        {
            auto result = popScope(scopePosition, ScopeKind::DoBody);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // next after_do_body
        goto after_do_body$no_emit;
    }
    // ifScope ScopeKind::CompoundStmt
    if (scopePosition[0] == ScopeKind::CompoundStmt) {
        // next statement
        goto statement$no_emit;
    }
    // ifScope ScopeKind::ContextFunction
    if (scopePosition[0] == ScopeKind::ContextFunction) {
        // popScope ScopeKind::ContextFunction
        {
            auto result = popScope(scopePosition, ScopeKind::ContextFunction);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // next statement
        goto statement$no_emit;
    }
    parseState = State::AfterStatement;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_statement$as_then:
    // then error
    goto error$as_then;

    // SwitchState statement
statement$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
statement$no_emit:
    parseState = State::Statement;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
statement$retry:
    tokEnd = skipWhitespace(tokEnd);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED statement$as_then:
    switch (tokEnd[0]) {
    case '\n': {
        tokEnd += 1;
        markLineBegin(tokEnd, output);
        goto statement$retry;
    }
    case '\r': {
        if (tokEnd[1] == '\n') {
            tokEnd += 2;
        } else {
            tokEnd += 1;
        }
        markLineBegin(tokEnd, output);
        goto statement$retry;
    }
    case '!': {
        if (tokEnd[1] == '=') {
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
        carriedEmitTokenKind = TokenKind::LogicalNotExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '%': {
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == '&') {
            if (tokEnd[2] == '=') {
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
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == '=') {
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
        carriedEmitTokenKind = TokenKind::DereferenceExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    case '+': {
        if (tokEnd[1] == '+') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // emitToken TokenKind::PreIncrementExpr
            carriedEmitTokenKind = TokenKind::PreIncrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == '-') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // emitToken TokenKind::PreDecrementExpr
            carriedEmitTokenKind = TokenKind::PreDecrementExpr;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '>') {
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
        carriedEmitTokenKind = TokenKind::ImplicitSelfReference;
        carriedEmitTokenData = 0;
        // next member_access
        goto member_access$with_emit;
    }
    case '/': {
        if (tokEnd[1] == '*') {
            tokEnd += 2;
            tokEnd = skipToEndOfBlockComment(tokEnd);
            tokEnd += 2;
            emitWhitespace(WhitespaceKind::BlockComment, tokBegin, tokEnd, output);
            goto statement$retry;
        }
        if (tokEnd[1] == '/') {
            tokEnd += 2;
            tokEnd = skipToEndOfLine(tokEnd);
            emitWhitespace(WhitespaceKind::LineComment, tokBegin, tokEnd, output);
            goto statement$retry;
        }
        if (tokEnd[1] == '=') {
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
        if (tokEnd[1] == ':') {
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
        if (tokEnd[1] == '<') {
            if (tokEnd[2] == '=') {
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
        if (tokEnd[1] == '=') {
            if (tokEnd[2] == '>') {
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
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '>') {
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
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '>') {
            if (tokEnd[2] == '=') {
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
        if (tokEnd[1] == '=') {
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
        carriedEmitTokenKind = TokenKind::CompoundStmt;
        carriedEmitTokenData = 0;
        // next statement
        goto statement$with_emit;
    }
    case '|': {
        if (tokEnd[1] == '=') {
            tokEnd += 2;
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // -> expression
            // -> error
            goto error$as_then;
        }
        if (tokEnd[1] == '|') {
            if (tokEnd[2] == '=') {
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
        // emitToken TokenKind::NumericLiteralExpr
        carriedEmitTokenKind = TokenKind::NumericLiteralExpr;
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
        // emitToken TokenKind::CharacterLiteralExpr
        carriedEmitTokenKind = TokenKind::CharacterLiteralExpr;
        carriedEmitTokenData = 0;
        // next after_expression
        goto after_expression$with_emit;
    }
    case '"': {
        tokEnd = skipToEndOfStringLiteral(tokEnd);
        if (tokEnd[0] != '"') {
            goto error$as_then;
        }
        tokEnd += 1;
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::StringLiteralExpr
        carriedEmitTokenKind = TokenKind::StringLiteralExpr;
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
        goto statement$word_case_with_read;
    default: {
        VERIFY_NOT_REACHED();
    }
    } // switch
    VERIFY_NOT_REACHED();
LABEL_MAYBE_UNUSED statement$word_case_with_read:
    {
        auto wordAndPos = readWord(tokEnd, output);
        tokEnd = wordAndPos.position;
        this_identifier = wordAndPos.word;
    }
LABEL_MAYBE_UNUSED statement$word_case:
    if (isIdentifierKeywordOrSpecial(this_identifier)) {
        switch (toSwitchValue(this_identifier)) {
        case toCaseValue<identifier_t>(LexerToken::Assert, "assert"):
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AssertStmt
            carriedEmitTokenKind = TokenKind::AssertStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        case toCaseValue<identifier_t>(LexerToken::Break, "break"):
            // emitToken TokenKind::BreakStmt
            carriedEmitTokenKind = TokenKind::BreakStmt;
            carriedEmitTokenData = 0;
            // next after_loop_control
            goto after_loop_control$with_emit;
        case toCaseValue<identifier_t>(LexerToken::Continue, "continue"):
            // emitToken TokenKind::ContinueStmt
            carriedEmitTokenKind = TokenKind::ContinueStmt;
            carriedEmitTokenData = 0;
            // next after_loop_control
            goto after_loop_control$with_emit;
        case toCaseValue<identifier_t>(LexerToken::Destroy, "destroy"):
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::DestroyStmt
            carriedEmitTokenKind = TokenKind::DestroyStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        case toCaseValue<identifier_t>(LexerToken::Discard, "discard"):
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::DiscardStmt
            carriedEmitTokenKind = TokenKind::DiscardStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        case toCaseValue<identifier_t>(LexerToken::Do, "do"):
            // emitToken TokenKind::DoStmt
            carriedEmitTokenKind = TokenKind::DoStmt;
            carriedEmitTokenData = 0;
            // next do_body
            goto do_body$with_emit;
        case toCaseValue<identifier_t>(LexerToken::For, "for"):
            // pushScope ScopeKind::ForVariable
            scopePosition = pushScope(scopePosition, ScopeKind::ForVariable);
            // emitToken TokenKind::ForStmt
            carriedEmitTokenKind = TokenKind::ForStmt;
            carriedEmitTokenData = 0;
            // next for_variable
            goto for_variable$with_emit;
        case toCaseValue<identifier_t>(LexerToken::If, "if"):
            // pushScope ScopeKind::LeftExpr
            scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
            // pushScope ScopeKind::IfExprOrStmt
            scopePosition = pushScope(scopePosition, ScopeKind::IfExprOrStmt);
            // next expression
            goto expression$no_emit;
        case toCaseValue<identifier_t>(LexerToken::Let, "let"):
            // next let_statement
            goto let_statement$no_emit;
        case toCaseValue<identifier_t>(LexerToken::Loop, "loop"):
            // emitToken TokenKind::LoopStmt
            carriedEmitTokenKind = TokenKind::LoopStmt;
            carriedEmitTokenData = 0;
            // next loop_body
            goto loop_body$with_emit;
        case toCaseValue<identifier_t>(LexerToken::Prove, "prove"):
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::ProveStmt
            carriedEmitTokenKind = TokenKind::ProveStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        case toCaseValue<identifier_t>(LexerToken::Return, "return"):
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::ReturnStmt
            carriedEmitTokenKind = TokenKind::ReturnStmt;
            carriedEmitTokenData = 0;
            // next after_return
            goto after_return$with_emit;
        case toCaseValue<identifier_t>(LexerToken::Var, "var"):
            // next var_statement
            goto var_statement$no_emit;
        case toCaseValue<identifier_t>(LexerToken::While, "while"):
            // pushScope ScopeKind::WhileCondition
            scopePosition = pushScope(scopePosition, ScopeKind::WhileCondition);
            // emitToken TokenKind::WhileStmt
            carriedEmitTokenKind = TokenKind::WhileStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        default:
            if (isKeyword(this_identifier)) {
                goto error$as_then;
            }
            break;
        }
    }
    // pushScope ScopeKind::LeftExpr
    scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
    // -> expression
    // emitToken TokenKind::IdentifierExpr
    carriedEmitTokenKind = TokenKind::IdentifierExpr;
    carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
    // next after_expression
    goto after_expression$with_emit;

    // LinearState let_statement
let_statement$no_emit:
    parseState = State::LetStatement;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED let_statement$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED let_statement$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED let_statement$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::LetValueDecl
        carriedEmitTokenKind = TokenKind::LetValueDecl;
        carriedEmitTokenData = packData1(TokenKind::LetValueDecl, this_identifier);
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState var_statement
var_statement$no_emit:
    parseState = State::VarStatement;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED var_statement$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED var_statement$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED var_statement$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::VarValueDecl
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
    parseState = State::AfterReturn;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_return$as_then:
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
        setBackKind(output, TokenKind::EmptyReturnStmt);
        // next after_statement
        goto after_statement$no_emit;
    }
    // then expression
    goto expression$as_then;

    // LinearState check_else_branch
check_else_branch$no_emit:
    parseState = State::CheckElseBranch;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED check_else_branch$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED check_else_branch$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED check_else_branch$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Assert, "assert"):
                // -> statement
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::AssertStmt
                carriedEmitTokenKind = TokenKind::AssertStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Break, "break"):
                // -> statement
                // emitToken TokenKind::BreakStmt
                carriedEmitTokenKind = TokenKind::BreakStmt;
                carriedEmitTokenData = 0;
                // next after_loop_control
                goto after_loop_control$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Continue, "continue"):
                // -> statement
                // emitToken TokenKind::ContinueStmt
                carriedEmitTokenKind = TokenKind::ContinueStmt;
                carriedEmitTokenData = 0;
                // next after_loop_control
                goto after_loop_control$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Destroy, "destroy"):
                // -> statement
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::DestroyStmt
                carriedEmitTokenKind = TokenKind::DestroyStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Discard, "discard"):
                // -> statement
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::DiscardStmt
                carriedEmitTokenKind = TokenKind::DiscardStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Do, "do"):
                // -> statement
                // emitToken TokenKind::DoStmt
                carriedEmitTokenKind = TokenKind::DoStmt;
                carriedEmitTokenData = 0;
                // next do_body
                goto do_body$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Else, "else"):
                // pushScope ScopeKind::ElseBranch
                scopePosition = pushScope(scopePosition, ScopeKind::ElseBranch);
                // next else_branch
                goto else_branch$no_emit;
            case toCaseValue<identifier_t>(LexerToken::For, "for"):
                // -> statement
                // pushScope ScopeKind::ForVariable
                scopePosition = pushScope(scopePosition, ScopeKind::ForVariable);
                // emitToken TokenKind::ForStmt
                carriedEmitTokenKind = TokenKind::ForStmt;
                carriedEmitTokenData = 0;
                // next for_variable
                goto for_variable$with_emit;
            case toCaseValue<identifier_t>(LexerToken::If, "if"):
                // -> statement
                // pushScope ScopeKind::LeftExpr
                scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
                // pushScope ScopeKind::IfExprOrStmt
                scopePosition = pushScope(scopePosition, ScopeKind::IfExprOrStmt);
                // next expression
                goto expression$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Let, "let"):
                // -> statement
                // next let_statement
                goto let_statement$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Loop, "loop"):
                // -> statement
                // emitToken TokenKind::LoopStmt
                carriedEmitTokenKind = TokenKind::LoopStmt;
                carriedEmitTokenData = 0;
                // next loop_body
                goto loop_body$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Prove, "prove"):
                // -> statement
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::ProveStmt
                carriedEmitTokenKind = TokenKind::ProveStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Return, "return"):
                // -> statement
                // pushScope ScopeKind::RightExpr
                scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
                // emitToken TokenKind::ReturnStmt
                carriedEmitTokenKind = TokenKind::ReturnStmt;
                carriedEmitTokenData = 0;
                // next after_return
                goto after_return$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Var, "var"):
                // -> statement
                // next var_statement
                goto var_statement$no_emit;
            case toCaseValue<identifier_t>(LexerToken::While, "while"):
                // -> statement
                // pushScope ScopeKind::WhileCondition
                scopePosition = pushScope(scopePosition, ScopeKind::WhileCondition);
                // emitToken TokenKind::WhileStmt
                carriedEmitTokenKind = TokenKind::WhileStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> statement
        // pushScope ScopeKind::LeftExpr
        scopePosition = pushScope(scopePosition, ScopeKind::LeftExpr);
        // -> expression
        // emitToken TokenKind::IdentifierExpr
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // then statement
    goto statement$as_then;

    // LinearState else_branch
else_branch$no_emit:
    parseState = State::ElseBranch;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED else_branch$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // emitToken TokenKind::ElseStmt
            carriedEmitTokenKind = TokenKind::ElseStmt;
            carriedEmitTokenData = 0;
            // next statement
            goto statement$with_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState do_body
do_body$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
do_body$no_emit:
    parseState = State::DoBody;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED do_body$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // pushScope ScopeKind::DoBody
            scopePosition = pushScope(scopePosition, ScopeKind::DoBody);
            // next statement
            goto statement$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState after_do_body
after_do_body$no_emit:
    parseState = State::AfterDoBody;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_do_body$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED after_do_body$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED after_do_body$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::While, "while"):
                // pushScope ScopeKind::DoWhileCondition
                scopePosition = pushScope(scopePosition, ScopeKind::DoWhileCondition);
                // emitToken TokenKind::DoWhileStmt
                carriedEmitTokenKind = TokenKind::DoWhileStmt;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> error
        goto error$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState loop_body
loop_body$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
loop_body$no_emit:
    parseState = State::LoopBody;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED loop_body$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // pushScope ScopeKind::LoopBody
            scopePosition = pushScope(scopePosition, ScopeKind::LoopBody);
            // next statement
            goto statement$no_emit;
        }
    }
    // then error
    goto error$as_then;

    // LinearState for_variable
for_variable$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
for_variable$no_emit:
    parseState = State::ForVariable;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED for_variable$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED for_variable$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED for_variable$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Var, "var"):
                // next var_parameter
                goto var_parameter$no_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::LetValueDecl
        carriedEmitTokenKind = TokenKind::LetValueDecl;
        carriedEmitTokenData = packData1(TokenKind::LetValueDecl, this_identifier);
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_loop_control
after_loop_control$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_loop_control$no_emit:
    parseState = State::AfterLoopControl;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_loop_control$as_then:
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // next after_statement
        goto after_statement$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_simple_variable_declaration_id
after_simple_variable_declaration_id$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_simple_variable_declaration_id$no_emit:
    parseState = State::AfterSimpleVariableDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_simple_variable_declaration_id$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // pushScope ScopeKind::VariableType
            scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
            // emitToken TokenKind::VariableType, sema::VariableKind::Let
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
    parseState = State::AfterVariableDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_variable_declaration_id$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // emitToken TokenKind::VariableType, sema::VariableKind::Let
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
            carriedEmitTokenKind = TokenKind::AssignStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // emitToken TokenKind::ExpressionStmt
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
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_parameters
        goto after_parameters$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED after_variable_declaration_id$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED after_variable_declaration_id$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::In, "in"):
                // popScope ScopeKind::ForVariable
                {
                    auto result = popScope(scopePosition, ScopeKind::ForVariable);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::ForRange
                scopePosition = pushScope(scopePosition, ScopeKind::ForRange);
                // emitToken TokenKind::ForIn
                carriedEmitTokenKind = TokenKind::ForIn;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> error
        goto error$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState variable_type
variable_type$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
variable_type$no_emit:
    parseState = State::VariableType;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED variable_type$as_then:
    if (std::string_view(tokEnd, 1) == "&"sv) {
        char next = tokEnd[1];
        if (next != '&' && next != '=') {
            tokEnd += 1;
            // next reference_modifier
            goto reference_modifier$no_emit;
        }
    }
    // pushScope ScopeKind::VariableType
    scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
    // then expression
    goto expression$as_then;

    // LinearState reference_modifier
reference_modifier$no_emit:
    parseState = State::ReferenceModifier;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED reference_modifier$as_then:
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // updateData sema::VariableKind::Generic
        setBackData1(output, sema::VariableKind::Generic);
        // pushScope ScopeKind::GenericCategoryExpression
        scopePosition = pushScope(scopePosition, ScopeKind::GenericCategoryExpression);
        // emitToken TokenKind::VariableGenericCategory
        carriedEmitTokenKind = TokenKind::VariableGenericCategory;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED reference_modifier$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED reference_modifier$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Const, "const"):
                // updateData sema::VariableKind::ConstSharedReference
                setBackData1(output, sema::VariableKind::ConstSharedReference);
                // next after_variable_const_modifier
                goto after_variable_const_modifier$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Shared, "shared"):
                // updateData sema::VariableKind::SharedReference
                setBackData1(output, sema::VariableKind::SharedReference);
                // next after_variable_shared_modifier
                goto after_variable_shared_modifier$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Unique, "unique"):
                // updateData sema::VariableKind::UniqueReference
                setBackData1(output, sema::VariableKind::UniqueReference);
                // next after_variable_unique_modifier
                goto after_variable_unique_modifier$no_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> error
        goto error$as_then;
    }
    // then error
    goto error$as_then;

    // LinearState after_reference_modifier
after_reference_modifier$no_emit:
    parseState = State::AfterReferenceModifier;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_reference_modifier$as_then:
    // ifScope ScopeKind::ReturnType
    if (scopePosition[0] == ScopeKind::ReturnType) {
        // then after_return_type_modifier
        goto after_return_type_modifier$as_then;
    }
    // then after_variable_modifier
    goto after_variable_modifier$as_then;

    // LinearState after_variable_modifier
LABEL_MAYBE_UNUSED after_variable_modifier$as_then:
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AssignStmt
            carriedEmitTokenKind = TokenKind::AssignStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // emitToken TokenKind::ExpressionStmt
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
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_parameters
        goto after_parameters$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED after_variable_modifier$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED after_variable_modifier$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::If, "if"):
                // pushScope ScopeKind::VariableType
                scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
                // -> expression
                // pushScope ScopeKind::IfExpr
                scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                // next expression
                goto expression$no_emit;
            case toCaseValue<identifier_t>(LexerToken::In, "in"):
                // popScope ScopeKind::ForVariable
                {
                    auto result = popScope(scopePosition, ScopeKind::ForVariable);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::ForRange
                scopePosition = pushScope(scopePosition, ScopeKind::ForRange);
                // emitToken TokenKind::ForIn
                carriedEmitTokenKind = TokenKind::ForIn;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        // emitToken TokenKind::IdentifierExpr
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // pushScope ScopeKind::VariableType
    scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
    // then expression
    goto expression$as_then;

    // LinearState after_variable_unique_modifier
after_variable_unique_modifier$no_emit:
    parseState = State::AfterVariableUniqueModifier;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_variable_unique_modifier$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED after_variable_unique_modifier$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED after_variable_unique_modifier$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Const, "const"):
                // updateData sema::VariableKind::ConstUniqueReference
                setBackData1(output, sema::VariableKind::ConstUniqueReference);
                // next after_reference_modifier
                goto after_reference_modifier$no_emit;
            case toCaseValue<identifier_t>(LexerToken::If, "if"):
                // -> after_reference_modifier
                // ifScope ScopeKind::ReturnType
                if (scopePosition[0] == ScopeKind::ReturnType) {
                    // then after_return_type_modifier
                    // -> expression
                    // pushScope ScopeKind::IfExpr
                    scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                    // next expression
                    goto expression$no_emit;
                }
                // -> after_variable_modifier
                // pushScope ScopeKind::VariableType
                scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
                // -> expression
                // pushScope ScopeKind::IfExpr
                scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                // next expression
                goto expression$no_emit;
            case toCaseValue<identifier_t>(LexerToken::In, "in"):
                // -> after_reference_modifier
                // ifScope ScopeKind::ReturnType
                if (scopePosition[0] == ScopeKind::ReturnType) {
                    // then after_return_type_modifier
                    // -> expression
                    // -> error
                    goto error$as_then;
                }
                // -> after_variable_modifier
                // popScope ScopeKind::ForVariable
                {
                    auto result = popScope(scopePosition, ScopeKind::ForVariable);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::ForRange
                scopePosition = pushScope(scopePosition, ScopeKind::ForRange);
                // emitToken TokenKind::ForIn
                carriedEmitTokenKind = TokenKind::ForIn;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> after_reference_modifier
        // ifScope ScopeKind::ReturnType
        if (scopePosition[0] == ScopeKind::ReturnType) {
            // then after_return_type_modifier
            // -> expression
            // emitToken TokenKind::IdentifierExpr
            carriedEmitTokenKind = TokenKind::IdentifierExpr;
            carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
            // next after_expression
            goto after_expression$with_emit;
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        // emitToken TokenKind::IdentifierExpr
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // then after_reference_modifier
    goto after_reference_modifier$as_then;

    // LinearState after_variable_shared_modifier
after_variable_shared_modifier$no_emit:
    parseState = State::AfterVariableSharedModifier;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_variable_shared_modifier$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED after_variable_shared_modifier$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED after_variable_shared_modifier$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Const, "const"):
                // updateData sema::VariableKind::ConstSharedReference
                setBackData1(output, sema::VariableKind::ConstSharedReference);
                // next after_reference_modifier
                goto after_reference_modifier$no_emit;
            case toCaseValue<identifier_t>(LexerToken::If, "if"):
                // -> after_reference_modifier
                // ifScope ScopeKind::ReturnType
                if (scopePosition[0] == ScopeKind::ReturnType) {
                    // then after_return_type_modifier
                    // -> expression
                    // pushScope ScopeKind::IfExpr
                    scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                    // next expression
                    goto expression$no_emit;
                }
                // -> after_variable_modifier
                // pushScope ScopeKind::VariableType
                scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
                // -> expression
                // pushScope ScopeKind::IfExpr
                scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                // next expression
                goto expression$no_emit;
            case toCaseValue<identifier_t>(LexerToken::In, "in"):
                // -> after_reference_modifier
                // ifScope ScopeKind::ReturnType
                if (scopePosition[0] == ScopeKind::ReturnType) {
                    // then after_return_type_modifier
                    // -> expression
                    // -> error
                    goto error$as_then;
                }
                // -> after_variable_modifier
                // popScope ScopeKind::ForVariable
                {
                    auto result = popScope(scopePosition, ScopeKind::ForVariable);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::ForRange
                scopePosition = pushScope(scopePosition, ScopeKind::ForRange);
                // emitToken TokenKind::ForIn
                carriedEmitTokenKind = TokenKind::ForIn;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> after_reference_modifier
        // ifScope ScopeKind::ReturnType
        if (scopePosition[0] == ScopeKind::ReturnType) {
            // then after_return_type_modifier
            // -> expression
            // emitToken TokenKind::IdentifierExpr
            carriedEmitTokenKind = TokenKind::IdentifierExpr;
            carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
            // next after_expression
            goto after_expression$with_emit;
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        // emitToken TokenKind::IdentifierExpr
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // then after_reference_modifier
    goto after_reference_modifier$as_then;

    // LinearState after_variable_const_modifier
after_variable_const_modifier$no_emit:
    parseState = State::AfterVariableConstModifier;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_variable_const_modifier$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED after_variable_const_modifier$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED after_variable_const_modifier$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::If, "if"):
                // -> after_reference_modifier
                // ifScope ScopeKind::ReturnType
                if (scopePosition[0] == ScopeKind::ReturnType) {
                    // then after_return_type_modifier
                    // -> expression
                    // pushScope ScopeKind::IfExpr
                    scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                    // next expression
                    goto expression$no_emit;
                }
                // -> after_variable_modifier
                // pushScope ScopeKind::VariableType
                scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
                // -> expression
                // pushScope ScopeKind::IfExpr
                scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                // next expression
                goto expression$no_emit;
            case toCaseValue<identifier_t>(LexerToken::In, "in"):
                // -> after_reference_modifier
                // ifScope ScopeKind::ReturnType
                if (scopePosition[0] == ScopeKind::ReturnType) {
                    // then after_return_type_modifier
                    // -> expression
                    // -> error
                    goto error$as_then;
                }
                // -> after_variable_modifier
                // popScope ScopeKind::ForVariable
                {
                    auto result = popScope(scopePosition, ScopeKind::ForVariable);
                    if (result == nullptr) {
                        goto pop_scope_failed;
                    }
                    scopePosition = result;
                }
                // pushScope ScopeKind::ForRange
                scopePosition = pushScope(scopePosition, ScopeKind::ForRange);
                // emitToken TokenKind::ForIn
                carriedEmitTokenKind = TokenKind::ForIn;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Shared, "shared"):
                // next after_reference_modifier
                goto after_reference_modifier$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Unique, "unique"):
                // updateData sema::VariableKind::ConstUniqueReference
                setBackData1(output, sema::VariableKind::ConstUniqueReference);
                // next after_reference_modifier
                goto after_reference_modifier$no_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> after_reference_modifier
        // ifScope ScopeKind::ReturnType
        if (scopePosition[0] == ScopeKind::ReturnType) {
            // then after_return_type_modifier
            // -> expression
            // emitToken TokenKind::IdentifierExpr
            carriedEmitTokenKind = TokenKind::IdentifierExpr;
            carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
            // next after_expression
            goto after_expression$with_emit;
        }
        // -> after_variable_modifier
        // pushScope ScopeKind::VariableType
        scopePosition = pushScope(scopePosition, ScopeKind::VariableType);
        // -> expression
        // emitToken TokenKind::IdentifierExpr
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // then after_reference_modifier
    goto after_reference_modifier$as_then;

    // LinearState return_type
return_type$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
return_type$no_emit:
    parseState = State::ReturnType;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED return_type$as_then:
    if (std::string_view(tokEnd, 1) == "&"sv) {
        char next = tokEnd[1];
        if (next != '&' && next != '=') {
            tokEnd += 1;
            // next reference_modifier
            goto reference_modifier$no_emit;
        }
    }
    // then expression
    goto expression$as_then;

    // LinearState after_return_type_modifier
LABEL_MAYBE_UNUSED after_return_type_modifier$as_then:
    if (std::string_view(tokEnd, 1) == "["sv) {
        tokEnd += 1;
        // pushScope ScopeKind::BorrowList
        scopePosition = pushScope(scopePosition, ScopeKind::BorrowList);
        // emitToken TokenKind::BorrowList
        carriedEmitTokenKind = TokenKind::BorrowList;
        carriedEmitTokenData = 0;
        // next borrow
        goto borrow$with_emit;
    }
    // then expression
    goto expression$as_then;

    // LinearState borrow
borrow$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
borrow$no_emit:
    parseState = State::Borrow;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED borrow$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED borrow$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED borrow$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::If, "if"):
                // emitToken TokenKind::Borrow
                emitToken(TokenKind::Borrow, tokBegin, 0, output);
                // -> expression
                // pushScope ScopeKind::IfExpr
                scopePosition = pushScope(scopePosition, ScopeKind::IfExpr);
                // next expression
                goto expression$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Unique, "unique"):
                // emitToken TokenKind::UniqueBorrow
                carriedEmitTokenKind = TokenKind::UniqueBorrow;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::Borrow
        emitToken(TokenKind::Borrow, tokBegin, 0, output);
        // -> expression
        // emitToken TokenKind::IdentifierExpr
        carriedEmitTokenKind = TokenKind::IdentifierExpr;
        carriedEmitTokenData = packData1(TokenKind::IdentifierExpr, this_identifier);
        // next after_expression
        goto after_expression$with_emit;
    }
    // emitToken TokenKind::Borrow
    emitToken(TokenKind::Borrow, tokBegin, 0, output);
    // then expression
    goto expression$as_then;

    // LinearState after_parameters
after_parameters$with_emit:
    emitToken(carriedEmitTokenKind, tokBegin, carriedEmitTokenData, output);
after_parameters$no_emit:
    // emitToken TokenKind::EmptyNode
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
        // next after_function_parameters
        goto after_function_parameters$no_emit;
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
        // next after_template_parameters
        goto after_template_parameters$no_emit;
    }
    parseState = State::AfterParameters;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_parameters$as_then:
    // then error
    goto error$as_then;

    // LinearState first_parameter
first_parameter$no_emit:
    parseState = State::FirstParameter;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED first_parameter$as_then:
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
    parseState = State::Parameter;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED parameter$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED parameter$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED parameter$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Var, "var"):
                // next var_parameter
                goto var_parameter$no_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::LetValueDecl
        carriedEmitTokenKind = TokenKind::LetValueDecl;
        carriedEmitTokenData = packData1(TokenKind::LetValueDecl, this_identifier);
        // next after_variable_declaration_id
        goto after_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState var_parameter
var_parameter$no_emit:
    parseState = State::VarParameter;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED var_parameter$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED var_parameter$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED var_parameter$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::VarValueDecl
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
    parseState = State::ImplExpression;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED impl_expression$as_then:
    if (std::string_view(tokEnd, 1) == "("sv) {
        tokEnd += 1;
        // pushScope ScopeKind::ParenInImplExpr
        scopePosition = pushScope(scopePosition, ScopeKind::ParenInImplExpr);
        // emitToken TokenKind::ParenthesizedExpr
        carriedEmitTokenKind = TokenKind::ParenthesizedExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED impl_expression$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED impl_expression$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::IdentifierExpr
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
    parseState = State::AfterImplExpression;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_impl_expression$as_then:
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
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState impl_access_expression
impl_access_expression$no_emit:
    parseState = State::ImplAccessExpression;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED impl_access_expression$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED impl_access_expression$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED impl_access_expression$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // emitToken TokenKind::StaticAccessExpr
        carriedEmitTokenKind = TokenKind::StaticAccessExpr;
        carriedEmitTokenData = packData1(TokenKind::StaticAccessExpr, this_identifier);
        // next after_impl_expression
        goto after_impl_expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState no_declaration
LABEL_MAYBE_UNUSED no_declaration$as_then:
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
        // popScope ScopeKind::Namespace
        {
            auto result = popScope(scopePosition, ScopeKind::Namespace);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        // popScope ScopeKind::Start
        {
            auto result = popScope(scopePosition, ScopeKind::Start);
            if (result == nullptr) {
                goto pop_scope_failed;
            }
            scopePosition = result;
        }
        emitToken(TokenKind::EOS, tokBegin, 0, output);
        emitWhitespace(WhitespaceKind::EOS, tokBegin, tokEnd, output);
        goto exit;
    }
    // then error
    goto error$as_then;

    // LinearState namespace_declaration
namespace_declaration$no_emit:
    parseState = State::NamespaceDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED namespace_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED namespace_declaration$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED namespace_declaration$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Static, "static"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next after_static
                goto after_static$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Enum, "enum"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Fn, "fn"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Incomplete, "incomplete"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Namespace, "namespace"):
                // next namespace_declaration_id
                goto namespace_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Struct, "struct"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Template, "template"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::TemplateAttribute
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Virtual, "virtual"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // -> templated_declaration
        // -> no_declaration
        // -> error
        goto error$as_then;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState namespace_declaration_id
namespace_declaration_id$no_emit:
    parseState = State::NamespaceDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED namespace_declaration_id$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED namespace_declaration_id$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED namespace_declaration_id$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // rememberDeclarationBegin
        declarationBegin = output.tokenBuffer.currentToken();
        // commitDeclaration DeclarationKind::Namespace, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Namespace>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::NamespaceDecl
        emitToken(TokenKind::NamespaceDecl, tokBegin, packData1(TokenKind::NamespaceDecl, this_identifier), output);
        // updateSecondaryData this_declaration
        setBackData2(output, this_declaration);
        // next after_namespace_declaration_id
        goto after_namespace_declaration_id$no_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_namespace_declaration_id
after_namespace_declaration_id$no_emit:
    parseState = State::AfterNamespaceDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_namespace_declaration_id$as_then:
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
    parseState = State::NamespaceDeclarationBody;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED namespace_declaration_body$as_then:
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
LABEL_MAYBE_UNUSED templated_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED templated_declaration$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED templated_declaration$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Static, "static"):
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next after_static
                goto after_static$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Enum, "enum"):
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Fn, "fn"):
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Incomplete, "incomplete"):
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Struct, "struct"):
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Template, "template"):
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::TemplateAttribute
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Virtual, "virtual"):
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
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
    parseState = State::TemplatedDeclarationWithAttributes;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED templated_declaration_with_attributes$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED templated_declaration_with_attributes$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED templated_declaration_with_attributes$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Static, "static"):
                // next after_static
                goto after_static$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Enum, "enum"):
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Fn, "fn"):
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Incomplete, "incomplete"):
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Struct, "struct"):
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Template, "template"):
                // emitToken TokenKind::TemplateAttribute
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Virtual, "virtual"):
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
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
    parseState = State::AfterTemplate;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_template$as_then:
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
    // next templated_declaration_with_attributes
    goto templated_declaration_with_attributes$no_emit;

    // LinearState function_declaration_id
function_declaration_id$no_emit:
    parseState = State::FunctionDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED function_declaration_id$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED function_declaration_id$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED function_declaration_id$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Impl, "impl"):
                // commitImplDeclaration DeclarationKind::Function
                this_declaration = commitImplDeclaration<DeclarationKind::Function>(tokBegin, declarationBegin, output);
                // pushScope ScopeKind::FunctionImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::FunctionImplExpression);
                // emitToken TokenKind::FunctionImplDecl
                carriedEmitTokenKind = TokenKind::FunctionImplDecl;
                carriedEmitTokenData = 0;
                // next impl_expression
                goto impl_expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // commitDeclaration DeclarationKind::Function, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Function>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::FunctionDecl
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
    parseState = State::AfterFunctionDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_function_declaration_id$as_then:
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
    parseState = State::AfterFunctionParameters;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_function_parameters$as_then:
    if (std::string_view(tokEnd, 1) == ":"sv) {
        char next = tokEnd[1];
        if (next != ':') {
            tokEnd += 1;
            // pushScope ScopeKind::FunctionBody
            scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
            // emitToken TokenKind::FunctionBody
            carriedEmitTokenKind = TokenKind::FunctionBody;
            carriedEmitTokenData = 0;
            // next statement
            goto statement$with_emit;
        }
    }
    if (std::string_view(tokEnd, 2) == "->"sv) {
        tokEnd += 2;
        // pushScope ScopeKind::ReturnType
        scopePosition = pushScope(scopePosition, ScopeKind::ReturnType);
        // emitToken TokenKind::ReturnType, sema::VariableKind::Let
        carriedEmitTokenKind = TokenKind::ReturnType;
        carriedEmitTokenData = packData1(TokenKind::ReturnType, sema::VariableKind::Let);
        // next return_type
        goto return_type$with_emit;
    }
    if (std::string_view(tokEnd, 2) == "=>"sv) {
        tokEnd += 2;
        // pushScope ScopeKind::FunctionBody
        scopePosition = pushScope(scopePosition, ScopeKind::FunctionBody);
        // pushScope ScopeKind::RightExpr
        scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
        // emitToken TokenKind::BodyExpr
        carriedEmitTokenKind = TokenKind::BodyExpr;
        carriedEmitTokenData = 0;
        // next expression
        goto expression$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState struct_declaration_id
struct_declaration_id$no_emit:
    parseState = State::StructDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED struct_declaration_id$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED struct_declaration_id$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED struct_declaration_id$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Impl, "impl"):
                // commitImplDeclaration DeclarationKind::Struct
                this_declaration = commitImplDeclaration<DeclarationKind::Struct>(tokBegin, declarationBegin, output);
                // pushScope ScopeKind::StructImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::StructImplExpression);
                // emitToken TokenKind::StructImplDecl
                carriedEmitTokenKind = TokenKind::StructImplDecl;
                carriedEmitTokenData = 0;
                // next impl_expression
                goto impl_expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // commitDeclaration DeclarationKind::Struct, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Struct>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::StructDecl
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
    parseState = State::AfterStructDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_struct_declaration_id$as_then:
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
    parseState = State::StructDeclarationBody;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED struct_declaration_body$as_then:
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
    parseState = State::MemberDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED member_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED member_declaration$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED member_declaration$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Static, "static"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next after_static
                goto after_static$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Base, "base"):
                // pushScope ScopeKind::BaseTypeExpr
                scopePosition = pushScope(scopePosition, ScopeKind::BaseTypeExpr);
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // commitDeclaration DeclarationKind::BaseMember
                this_declaration = commitDeclaration<DeclarationKind::BaseMember>(identifier_t(), tokBegin, declarationBegin, output);
                // emitToken TokenKind::BaseMemberDecl
                carriedEmitTokenKind = TokenKind::BaseMemberDecl;
                carriedEmitTokenData = 0;
                // next expression
                goto expression$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Enum, "enum"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Fn, "fn"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Incomplete, "incomplete"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Struct, "struct"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Template, "template"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::TemplateAttribute
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Virtual, "virtual"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // rememberDeclarationBegin
        declarationBegin = output.tokenBuffer.currentToken();
        // commitDeclaration DeclarationKind::Member, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Member>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::MemberDecl
        carriedEmitTokenKind = TokenKind::MemberDecl;
        carriedEmitTokenData = packData1(TokenKind::MemberDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then templated_declaration
    goto templated_declaration$as_then;

    // LinearState enum_declaration_id
enum_declaration_id$no_emit:
    parseState = State::EnumDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED enum_declaration_id$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED enum_declaration_id$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED enum_declaration_id$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Impl, "impl"):
                // commitImplDeclaration DeclarationKind::Enum
                this_declaration = commitImplDeclaration<DeclarationKind::Enum>(tokBegin, declarationBegin, output);
                // pushScope ScopeKind::EnumImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::EnumImplExpression);
                // emitToken TokenKind::EnumImplDecl
                carriedEmitTokenKind = TokenKind::EnumImplDecl;
                carriedEmitTokenData = 0;
                // next impl_expression
                goto impl_expression$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // commitDeclaration DeclarationKind::Enum, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::Enum>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::EnumDecl
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
    parseState = State::AfterEnumDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_enum_declaration_id$as_then:
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
    parseState = State::EnumDeclarationBody;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED enum_declaration_body$as_then:
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
    parseState = State::EnumValueDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED enum_value_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED enum_value_declaration$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED enum_value_declaration$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Static, "static"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next after_static
                goto after_static$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Enum, "enum"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next enum_declaration_id
                goto enum_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Fn, "fn"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next function_declaration_id
                goto function_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Incomplete, "incomplete"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::IncompleteAttribute
                carriedEmitTokenKind = TokenKind::IncompleteAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Struct, "struct"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // next struct_declaration_id
                goto struct_declaration_id$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Template, "template"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::TemplateAttribute
                carriedEmitTokenKind = TokenKind::TemplateAttribute;
                carriedEmitTokenData = 0;
                // next after_template
                goto after_template$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Virtual, "virtual"):
                // -> templated_declaration
                // rememberDeclarationBegin
                declarationBegin = output.tokenBuffer.currentToken();
                // emitToken TokenKind::VirtualAttribute
                carriedEmitTokenKind = TokenKind::VirtualAttribute;
                carriedEmitTokenData = 0;
                // next templated_declaration_with_attributes
                goto templated_declaration_with_attributes$with_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // rememberDeclarationBegin
        declarationBegin = output.tokenBuffer.currentToken();
        // commitDeclaration DeclarationKind::EnumValue, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::EnumValue>(this_identifier, tokBegin, declarationBegin, output);
        // emitToken TokenKind::ImplicitEnumValueDecl
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
    parseState = State::AfterEnumValueDeclarationId;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_enum_value_declaration_id$as_then:
    if (std::string_view(tokEnd, 1) == "="sv) {
        char next = tokEnd[1];
        if (next != '=' && next != '>') {
            tokEnd += 1;
            // updateKind TokenKind::ExplicitEnumValueDecl
            setBackKind(output, TokenKind::ExplicitEnumValueDecl);
            // pushScope ScopeKind::RightExpr
            scopePosition = pushScope(scopePosition, ScopeKind::RightExpr);
            // emitToken TokenKind::AssignStmt
            carriedEmitTokenKind = TokenKind::AssignStmt;
            carriedEmitTokenData = 0;
            // next expression
            goto expression$with_emit;
        }
    }
    if (std::string_view(tokEnd, 1) == ";"sv) {
        tokEnd += 1;
        // emitToken TokenKind::ExpressionStmt
        carriedEmitTokenKind = TokenKind::ExpressionStmt;
        carriedEmitTokenData = 0;
        // next after_declaration
        goto after_declaration$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState after_static
after_static$no_emit:
    parseState = State::AfterStatic;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_static$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED after_static$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED after_static$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            case toCaseValue<identifier_t>(LexerToken::Impl, "impl"):
                // commitImplDeclaration DeclarationKind::StaticVariable
                this_declaration = commitImplDeclaration<DeclarationKind::StaticVariable>(tokBegin, declarationBegin, output);
                // pushScope ScopeKind::GlobalImplExpression
                scopePosition = pushScope(scopePosition, ScopeKind::GlobalImplExpression);
                // emitToken TokenKind::GlobalImplDecl
                carriedEmitTokenKind = TokenKind::GlobalImplDecl;
                carriedEmitTokenData = 0;
                // next impl_expression
                goto impl_expression$with_emit;
            case toCaseValue<identifier_t>(LexerToken::Var, "var"):
                // next static_var_variable_declaration
                goto static_var_variable_declaration$no_emit;
            case toCaseValue<identifier_t>(LexerToken::Open, "open"):
                // next static_open_variable_declaration
                goto static_open_variable_declaration$no_emit;
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, output);
        // setGlobalKind GlobalKind::Let
        setGlobalKind(output, GlobalKind::Let);
        // emitToken TokenKind::GlobalDecl
        carriedEmitTokenKind = TokenKind::GlobalDecl;
        carriedEmitTokenData = packData1(TokenKind::GlobalDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_var_variable_declaration
static_var_variable_declaration$no_emit:
    parseState = State::StaticVarVariableDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED static_var_variable_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED static_var_variable_declaration$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED static_var_variable_declaration$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, output);
        // setGlobalKind GlobalKind::Var
        setGlobalKind(output, GlobalKind::Var);
        // emitToken TokenKind::GlobalDecl
        carriedEmitTokenKind = TokenKind::GlobalDecl;
        carriedEmitTokenData = packData1(TokenKind::GlobalDecl, this_identifier);
        // next after_simple_variable_declaration_id
        goto after_simple_variable_declaration_id$with_emit;
    }
    // then error
    goto error$as_then;

    // LinearState static_open_variable_declaration
static_open_variable_declaration$no_emit:
    parseState = State::StaticOpenVariableDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED static_open_variable_declaration$as_then:
    if (isWordFirstCharacter(tokEnd[0])) {
    LABEL_MAYBE_UNUSED static_open_variable_declaration$word_case_with_read:
        {
            auto wordAndPos = readWord(tokEnd, output);
            tokEnd = wordAndPos.position;
            this_identifier = wordAndPos.word;
        }
    LABEL_MAYBE_UNUSED static_open_variable_declaration$word_case:
        if (isIdentifierKeywordOrSpecial(this_identifier)) {
            switch (toSwitchValue(this_identifier)) {
            default:
                if (isKeyword(this_identifier)) {
                    goto error$as_then;
                }
                break;
            }
        }
        // commitDeclaration DeclarationKind::StaticVariable, this_identifier
        this_declaration = commitDeclaration<DeclarationKind::StaticVariable>(this_identifier, tokBegin, declarationBegin, output);
        // setGlobalKind GlobalKind::OpenLet
        setGlobalKind(output, GlobalKind::OpenLet);
        // emitToken TokenKind::GlobalDecl
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
    // endDeclaration
    endDeclaration(output);
    // ifScope ScopeKind::Struct
    if (scopePosition[0] == ScopeKind::Struct) {
        // next member_declaration
        goto member_declaration$no_emit;
    }
    // ifScope ScopeKind::Namespace
    if (scopePosition[0] == ScopeKind::Namespace) {
        // next namespace_declaration
        goto namespace_declaration$no_emit;
    }
    // ifScope ScopeKind::Enum
    if (scopePosition[0] == ScopeKind::Enum) {
        // next enum_value_declaration
        goto enum_value_declaration$no_emit;
    }
    parseState = State::AfterDeclaration;
    if (tokenLimit == 0)
        goto reached_token_limit;
    tokenLimit -= 1;
    tokEnd = inlineAdvancer(tokEnd, output);
    tokBegin = tokEnd;
LABEL_MAYBE_UNUSED after_declaration$as_then:
    // then error
    goto error$as_then;


    ReturnStatus returnStatus;
pop_scope_failed:
    returnStatus = ReturnStatus::ScopeError;
    tokEnd = tokBegin;
    tokenLimit += 1; // Don't count the token that caused the error as parsed
    goto return_stmt;
error$as_then:
    returnStatus = ReturnStatus::UnhandledCase;
    tokEnd = tokBegin;
    tokenLimit += 1; // Don't count the token that caused the error as parsed
    goto return_stmt;
reached_token_limit:
    returnStatus = ReturnStatus::Ready;
    goto return_stmt;
exit:
    returnStatus = ReturnStatus::EOS;
    goto return_stmt;

return_stmt:
    Word argumentNameOut = {};
    if constexpr (std::is_same_v<identifier_t, Word>)
        argumentNameOut = argumentName;
    int_t actualParsedTokens = expectedParsedTokens - tokenLimit; // tokenLimit holds how many tokens remain to the limit
    return {
        returnStatus,
        parseState,
        (uint32_t)actualParsedTokens,
        declarationBegin,
        argumentNameOut,
        tokEnd,
        scopePosition,
        argumentPosition
    };
}

ReturnStatus Parser::parse(sema::Context& output, int_t tokenLimit) {
    m_state = parseImpl(m_state, output, tokenLimit);
    return status();
}

ReturnStatus SimpleParser::parse(SimpleOutput& output, int_t tokenLimit) {
    Parser::InternalState inState = {
        m_state.status,
        m_state.state,
        m_state.parsedTokens,
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
        outState.parsedTokens,
        outState.sourcePosition,
        outState.scopePosition
    };
    return status();
}

ReturnStatus SimpleParser::parse(const NoOutput& output, int_t tokenLimit) {
    Parser::InternalState inState = {
        m_state.status,
        m_state.state,
        m_state.parsedTokens,
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
        outState.parsedTokens,
        outState.sourcePosition,
        outState.scopePosition
    };
    return status();
}

LexerToken lexToken(const char*& position) {
    NoOutput output;
    return lexImpl(position, output);
}

LexerToken Parser::skipToken(sema::Context& output) {
    return lexImpl(m_state.sourcePosition, output);
}

LexerToken SimpleParser::skipToken(SimpleOutput& output) {
    return lexImpl(m_state.sourcePosition, output);
}

LexerToken SimpleParser::skipToken(const NoOutput& output) {
    return lexImpl(m_state.sourcePosition, output);
}

const char* advanceToToken(const char* position) {
    NoOutput output;
    return inlineAdvancer(position, output);
}

void Parser::advanceToToken(sema::Context& output) {
    m_state.sourcePosition = inlineAdvancer(m_state.sourcePosition, output);
}

void SimpleParser::advanceToToken(SimpleOutput& output) {
    m_state.sourcePosition = inlineAdvancer(m_state.sourcePosition, output);
}

void SimpleParser::advanceToToken(const NoOutput& output) {
    m_state.sourcePosition = inlineAdvancer(m_state.sourcePosition, output);
}

SourceLocation Parser::location(sema::Context& context) const {
    return locationInCurrentLine(sourcePosition(), context.tokenBuffer);
}

}

#include "lexer_switchAndBranch.cpp"
#include "lexer_table2Char.cpp"
#include "lexer_switchAndPatternTable.cpp"
#include "lexer_patternTable.cpp"
#include "lexer_hybrid.cpp"
#include "lexer_switchAndTable.cpp"