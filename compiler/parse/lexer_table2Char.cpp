
namespace parse {
static constexpr LexerToken binaryToUpdateOp(LexerToken in) {
    return LexerToken(std::to_underlying(in) - std::to_underlying(LexerToken::Plus)
        + std::to_underlying(LexerToken::PlusEqual));
}
}

namespace parse::lexer_lookup_table {

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
    LexerToken token;
    std::string_view spelling;
};
constexpr auto sortedPunctuations() {
    std::array<Punctuation, (size_t)LexerToken::LastPunctuation - (size_t)LexerToken::FirstPunctuation + 1> punctuations;
    for (LexerToken t = LexerToken::FirstPunctuation; t <= LexerToken::LastPunctuation; t = LexerToken(std::to_underlying(t) + 1)) {
        punctuations[std::to_underlying(t) - std::to_underlying(LexerToken::FirstPunctuation)] = { t, fixedSpelling(t) };
    }
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
    auto encodePunctuationExtenededByEqual = [](LexerToken baseToken) constexpr -> uint8_t {
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
                VERIFY(extension.token == LexerToken::LessEqualGreater);
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
    { '/', '*', Result::BlockComment },
    { '\n', Result::NewlineLF },
    { '\r', '\n', Result::NewlineCRLF },
});

constexpr Result lookup(char c0, char c1) {
    return (Result)table[((size_t)c0 << 7) | (size_t)c1];
}

}

namespace parse {

const char* lexTable2Char(const char* sourcePosition, std::vector<LexerToken>& output) {

    for (;;) {
        sourcePosition = skipWhitespace(sourcePosition);

        LexerToken tok = LexerToken::Invalid;

        char c0 = sourcePosition[0];
        char c1 = sourcePosition[1];
        if (c0 < 0 || c0 > 127) [[unlikely]] {
            VERIFY_NOT_REACHED();
        }
        if (c1 < 0 || c1 > 127) [[unlikely]] {
            VERIFY_NOT_REACHED();
        }

        auto lookupResult = lexer_lookup_table::lookup(c0, c1);

        switch (std::to_underlying(lookupResult)) {
            using Result = decltype(lookupResult);
#define CASE(bits) case bits:
#define CASE4(base) CASE(base | 0b00) CASE(base | 0b01) CASE(base | 0b10) CASE(base | 0b11)
#define CASE16(base) CASE4(base | 0b0000) CASE4(base | 0b0100) CASE4(base | 0b1000) CASE4(base | 0b1100)
#define CASE64(base) CASE16(base | 0b000000) CASE16(base | 0b010000) CASE16(base | 0b100000) CASE16(base | 0b110000)
            CASE64(0b1100'0000) {
                LexerToken baseToken = LexerToken(std::to_underlying(lookupResult) & (uint8_t)0b0011'1111);
                bool b = sourcePosition[2] == '=';
                tok = b ? binaryToUpdateOp(baseToken) : baseToken;
                sourcePosition += b ? 3 : 2;
                break;
            }
            CASE64(0b0100'0000)
            CASE64(0b1000'0000) {
                auto advance = std::to_underlying(lookupResult) >> 6;
                sourcePosition += advance;
                tok = LexerToken(std::to_underlying(lookupResult) & (uint8_t)0b0011'1111);
                break;
            }
        case std::to_underlying(Result::Invalid): {
            char c0 = sourcePosition[0];
            if (c0 == '\0') {
                return sourcePosition;
            }
            VERIFY_NOT_REACHED();
        }
        case std::to_underlying(Result::NewlineLF):
            sourcePosition += 1;
            continue;
        case std::to_underlying(Result::NewlineCRLF):
            sourcePosition += 2;
            continue;
        case std::to_underlying(Result::LineComment):
            sourcePosition = skipToEndOfLine(sourcePosition);
            continue;
        case std::to_underlying(Result::BlockComment):
            sourcePosition = skipToEndOfBlockComment(sourcePosition);
            if (sourcePosition[0] == '\0') [[unlikely]] {
                VERIFY_NOT_REACHED();
            }
            sourcePosition += 2;
            continue;
        case std::to_underlying(Result::PunctuationPossiblyDoubleArrow):
            if (sourcePosition[2] == '>') {
                tok = LexerToken::LessEqualGreater;
                sourcePosition += 3;
            } else {
                tok = LexerToken::LessEqual;
                sourcePosition += 2;
            }
            break;
        case std::to_underlying(Result::Word): {
            const char* tokBegin = sourcePosition;
            do {
                sourcePosition += 1;
            } while (isWordBulkCharacter(sourcePosition[0]));
            const auto* entry = KeywordTable::get(tokBegin, sourcePosition - tokBegin);
            tok = entry == nullptr ? LexerToken::Identifier : entry->token;
            break;
        }
        case std::to_underlying(Result::NumbericLiteral): {
            tok = LexerToken::NumericLiteral;
            // TODO: implement parsing num literals
            for (;;) {
                char c = sourcePosition[0];
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')
                    sourcePosition += 1;
                else
                    break;
            }
            break;
        }
        case std::to_underlying(Result::CharacterLiteral): {
            tok = LexerToken::CharacterLiteral;
            sourcePosition += 1;
            sourcePosition = skipToEndOfCharacterLiteral(sourcePosition);
            VERIFY(sourcePosition[0] == '\'');
            sourcePosition += 1;
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }
        output.push_back(tok);
    }
}

}