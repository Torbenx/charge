
namespace parse::lookup_table0 {

inline constexpr LexerToken LINE_COMMENT_PLACEHOLDER = (LexerToken)58;
inline constexpr LexerToken BLOCK_COMMENT_PLACEHOLDER = (LexerToken)59;
inline constexpr LexerToken LF_PLACEHOLDER = (LexerToken)60;
inline constexpr LexerToken CR_PLACEHOLDER = (LexerToken)61;
inline constexpr LexerToken EOS_PLACEHOLDER = (LexerToken)62;
inline constexpr LexerToken INVALID_PLACEHOLDER = (LexerToken)63;

struct Input {
    LexerToken bare;
    LexerToken repeat = bare;
    LexerToken equal = bare;
    LexerToken arrow = bare;
    LexerToken repeatEqual = repeat;
    LexerToken repeatArrow = repeat;
    LexerToken equalArrow = equal;
    LexerToken repeatEqualArrow = repeatEqual;
};
struct InputRange : Input {
    uint8_t first, last;
    constexpr InputRange(char target, Input input)
        : Input(input), first(target), last(target) { }
    constexpr InputRange(uint8_t first, uint8_t last, Input input)
        : Input(input), first(first), last(last) { }
};

struct Output {
    uint8_t token : 6 = 0;
    uint8_t advance : 2 = 0;
    constexpr Output()
        : Output(INVALID_PLACEHOLDER, 0) { }
    constexpr Output(LexerToken token, int_t advance)
        : token(std::to_underlying(token)), advance(advance) {
        VERIFY(std::to_underlying(token) < 64);
        VERIFY(advance < 4);
    }
};

static constexpr int_t getOffset(uint8_t character, bool repeat, bool equal, bool arrow) {
    return (size_t)character | ((size_t)repeat << 7) | ((size_t)equal << 8) | ((size_t)arrow << 9);
}

static constexpr auto makeTable(std::initializer_list<InputRange> inputs) {
    std::array<Output, 0x80 * 8> output = {};
    for (auto input : inputs) {
        for (uint8_t c = input.first; c <= input.last; c++) {
            int_t bareAdvance = 1;
            output[getOffset(c, false, false, false)] = { input.bare, 1 };
            int_t equalAdvance = bareAdvance + (input.equal == input.bare ? 0 : 1);
            output[getOffset(c, false, true, false)] = { input.equal, equalAdvance };
            output[getOffset(c, false, false, true)] = { input.arrow, bareAdvance + (input.arrow == input.bare ? 0 : 1) };
            output[getOffset(c, false, true, true)] = { input.equalArrow, equalAdvance + (input.equalArrow == input.equal ? 0 : 1) };
            int_t repeatAdvance = input.repeat == input.bare ? bareAdvance : bareAdvance + 1;
            output[getOffset(c, true, false, false)] = { input.repeat, repeatAdvance };
            int_t repeatEqualAdvance = repeatAdvance + (input.repeatEqual == input.repeat ? 0 : 1);
            output[getOffset(c, true, true, false)] = { input.repeatEqual, repeatEqualAdvance };
            output[getOffset(c, true, false, true)] = { input.repeatArrow, repeatAdvance + (input.repeatArrow == input.repeat ? 0 : 1) };
            output[getOffset(c, true, true, true)] = { input.repeatEqualArrow, repeatEqualAdvance + (input.repeatEqualArrow == input.repeatEqual ? 0 : 1) };
        }
    }
    return output;
}

static constexpr auto table = makeTable({
    { '(', { LexerToken::LeftParen } },
    { ')', { LexerToken::RightParen } },
    { '[', { LexerToken::LeftSquare } },
    { ']', { LexerToken::RightSquare } },
    { '{', { LexerToken::LeftBrace } },
    { '}', { LexerToken::RightBrace } },
    { ',', { LexerToken::Comma } },
    { '.', { LexerToken::Point } },
    { ';', { LexerToken::SemiColon } },
    { '~', { LexerToken::Tilde } },
    { ':', { .bare = LexerToken::Colon, .repeat = LexerToken::ColonColon } },

    { '!', { .bare = LexerToken::Exclaim, .equal = LexerToken::ExclaimEqual } },
    { '%', { .bare = LexerToken::Percent, .equal = LexerToken::PercentEqual } },
    { '*', { .bare = LexerToken::Star, .equal = LexerToken::StarEqual } },
    { '^', { .bare = LexerToken::Hat, .equal = LexerToken::HatEqual } },

    { '/', { .bare = LexerToken::Slash, .repeat = LINE_COMMENT_PLACEHOLDER, .equal = LexerToken::SlashEqual, .arrow = BLOCK_COMMENT_PLACEHOLDER } },

    { '=', { .bare = LexerToken::Equal, .repeat = LexerToken::EqualEqual, .arrow = LexerToken::EqualGreater } },

    { '+', { .bare = LexerToken::Plus, .repeat = LexerToken::PlusPlus, .equal = LexerToken::PlusEqual } },
    { '-', { .bare = LexerToken::Minus, .repeat = LexerToken::MinusMinus, .equal = LexerToken::MinusEqual, .arrow = LexerToken::MinusGreater } },

    { '&', { .bare = LexerToken::Amp, .repeat = LexerToken::AmpAmp, .equal = LexerToken::AmpEqual, .repeatEqual = LexerToken::AmpAmpEqual } },
    { '|', { .bare = LexerToken::Vert, .repeat = LexerToken::VertVert, .equal = LexerToken::VertEqual, .repeatEqual = LexerToken::VertVertEqual } },

    { '<', { .bare = LexerToken::Less, .repeat = LexerToken::LessLess, .equal = LexerToken::LessEqual, .repeatEqual = LexerToken::LessLessEqual, .equalArrow = LexerToken::LessEqualGreater } },
    { '>', { .bare = LexerToken::Greater, .repeat = LexerToken::GreaterGreater, .equal = LexerToken::GreaterEqual, .repeatEqual = LexerToken::GreaterGreaterEqual } },

    { '0', '9', { LexerToken::NumericLiteral } },
    { '\'', { LexerToken::CharacterLiteral } },
    { '\"', { LexerToken::StringLiteral } },
    { '$', { LexerToken::Identifier } },
    { '#', { LexerToken::Identifier } },
    { '_', { LexerToken::Identifier } },
    { 'a', 'z', { LexerToken::Identifier } },
    { 'A', 'Z', { LexerToken::Identifier } },

    { '\0', { EOS_PLACEHOLDER } },
    { '\r', { CR_PLACEHOLDER } },
    { '\n', { LF_PLACEHOLDER } },
});

static constexpr std::pair<LexerToken, int_t> lookup(uint8_t character, bool repeat, bool equal, bool arrow) {
    Output out = table[getOffset(character, repeat, equal, arrow)];
    return { (LexerToken)out.token, (int_t)out.advance };
}

}

namespace parse {

const char* lexPatternTable(const char* sourcePosition, SimpleTokenBuffer<LexerToken>& output) {
    using namespace lookup_table0;

    for (;;) {
        sourcePosition = skipWhitespace(sourcePosition);

        const char* tokBegin = sourcePosition;
        auto head = sourcePosition[0];

        bool repeat = sourcePosition[1] == head;
        bool equal = sourcePosition[1 + (repeat ? 1 : 0)] == '=';
        bool arrow = sourcePosition[1 + (repeat ? 1 : 0) + (equal ? 1 : 0)] == (head == '/' ? '*' : '>');
        auto [tok, advance] = lookup_table0::lookup(head, repeat, equal, arrow);

        switch (tok) {
        default:
            sourcePosition += advance;
            break;
        case INVALID_PLACEHOLDER:
            dbgln("invalid head = '{}' on line {}", head, output.lines.size());
            VERIFY_NOT_REACHED();
        case EOS_PLACEHOLDER:
            output.tokens.push_back({ LexerToken::EOS, locationInCurrentLine(sourcePosition, output) });
            return sourcePosition;
        case LF_PLACEHOLDER:
            sourcePosition += 1;
            output.addLine(sourcePosition);
            continue;
        case CR_PLACEHOLDER:
            if (sourcePosition[1] == '\n')
                sourcePosition += 2;
            else
                sourcePosition += 1;
            output.addLine(sourcePosition);
            continue;
        case LINE_COMMENT_PLACEHOLDER:
            sourcePosition = skipToEndOfLine(sourcePosition + 2);
            output.whitespace.push_back({ { WhitespaceKind::LineComment, locationInCurrentLine(tokBegin, output) }, uint32_t(sourcePosition - tokBegin) });
            continue;
        case BLOCK_COMMENT_PLACEHOLDER:
            sourcePosition = skipToEndOfBlockComment(sourcePosition + 2);
            if (sourcePosition[0] == '\0') [[unlikely]] {
                VERIFY_NOT_REACHED();
            }
            sourcePosition += 2;
            output.whitespace.push_back({ { WhitespaceKind::BlockComment, locationInCurrentLine(tokBegin, output) }, uint32_t(sourcePosition - tokBegin) });
            continue;
        case LexerToken::NumericLiteral:
            for (;;) {
                char c = sourcePosition[0];
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')
                    sourcePosition += 1;
                else
                    break;
            }
            break;
        case LexerToken::CharacterLiteral:
            sourcePosition = skipToEndOfCharacterLiteral(sourcePosition);
            VERIFY(sourcePosition[0] == '\'');
            sourcePosition += 1;
            break;
        case LexerToken::StringLiteral:
            sourcePosition = skipToEndOfStringLiteral(sourcePosition);
            VERIFY(sourcePosition[0] == '\"');
            sourcePosition += 1;
            break;
        case LexerToken::Identifier: {
            const char* tokBegin = sourcePosition;
            do {
                sourcePosition += 1;
            } while (isWordBulkCharacter(sourcePosition[0]));
            const auto* entry = KeywordTable::get(tokBegin, sourcePosition - tokBegin);
            tok = entry == nullptr ? LexerToken::Identifier : entry->token;
            break;
        }
        }
        output.tokens.push_back({ tok, locationInCurrentLine(tokBegin, output) });
    }
}

}