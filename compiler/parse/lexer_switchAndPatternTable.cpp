namespace parse::lookup_table1 {

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
    constexpr Output() = default;
    constexpr Output(LexerToken token, int_t advance)
        : token(std::to_underlying(token)), advance(advance) {
        VERIFY(std::to_underlying(token) < 64);
        VERIFY(advance < 4);
    }
};

static constexpr int_t getOffset(uint8_t character, bool repeat, bool equal, bool arrow) {
    VERIFY(character >= 0x20 && character < 0x40);
    return (character - 0x20) | (repeat << 5) | (equal << 6) | (arrow << 7);
}

static constexpr auto makeTable(std::initializer_list<InputRange> inputs) {
    std::array<Output, 0x20 * 8> output = {};
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
    { ',', { LexerToken::Comma } },
    { '.', { LexerToken::Point } },
    { ';', { LexerToken::SemiColon } },
    { ':', { .bare = LexerToken::Colon, .repeat = LexerToken::ColonColon } },

    { '!', { .bare = LexerToken::Exclaim, .equal = LexerToken::ExclaimEqual } },
    { '%', { .bare = LexerToken::Percent, .equal = LexerToken::PercentEqual } },
    { '*', { .bare = LexerToken::Star, .equal = LexerToken::StarEqual } },

    { '=', { .bare = LexerToken::Equal, .repeat = LexerToken::EqualEqual, .arrow = LexerToken::EqualGreater } },

    { '+', { .bare = LexerToken::Plus, .repeat = LexerToken::PlusPlus, .equal = LexerToken::PlusEqual } },
    { '-', { .bare = LexerToken::Minus, .repeat = LexerToken::MinusMinus, .equal = LexerToken::MinusEqual, .arrow = LexerToken::MinusGreater } },

    { '&', { .bare = LexerToken::Amp, .repeat = LexerToken::AmpAmp, .equal = LexerToken::AmpEqual, .repeatEqual = LexerToken::AmpAmpEqual } },

    { '<', { .bare = LexerToken::Less, .repeat = LexerToken::LessLess, .equal = LexerToken::LessEqual, .repeatEqual = LexerToken::LessLessEqual, .equalArrow = LexerToken::LessEqualGreater } },
    { '>', { .bare = LexerToken::Greater, .repeat = LexerToken::GreaterGreater, .equal = LexerToken::GreaterEqual, .repeatEqual = LexerToken::GreaterGreaterEqual } },

    { '0', '9', { LexerToken::NumericLiteral } },
    { '\'', { LexerToken::CharacterLiteral } },
    { '\"', { LexerToken::StringLiteral } },
    { '$', { LexerToken::Identifier } },
    { '#', { LexerToken::Identifier } },
});

static constexpr std::pair<LexerToken, int_t> lookup(uint8_t character, bool repeat, bool equal, bool arrow) {
    Output out = table[getOffset(character, repeat, equal, arrow)];
    return { (LexerToken)out.token, (int_t)out.advance };
}

}

namespace parse::lookup_table2 {

struct Input {
    uint8_t first;
    uint8_t last;
    LexerToken token;
    constexpr Input(uint8_t character, LexerToken token)
        : first(character), last(character), token(token) { }
    constexpr Input(uint8_t first, uint8_t last, LexerToken token)
        : first(first), last(last), token(token) { }
};

struct Output {
    LexerToken token : 8 = LexerToken::Invalid;
};

static constexpr int_t getOffset(uint8_t character) {
    VERIFY(character >= 0x40 && character < 0x80);
    return character - 0x40;
}

static constexpr auto makeTable(std::initializer_list<Input> inputs) {
    std::array<Output, 0x40> output = {};
    for (auto input : inputs) {
        for (uint8_t c = input.first; c <= input.last; c++) {
            output[getOffset(c)] = Output { input.token };
        }
    }
    return output;
}

static constexpr auto table = makeTable({
    { '[', LexerToken::LeftSquare },
    { ']', LexerToken::RightSquare },
    { '{', LexerToken::LeftBrace },
    { '}', LexerToken::RightBrace },
    { '~', LexerToken::Tilde },
});

static constexpr LexerToken lookup(uint8_t character) { return table[getOffset(character)].token; }

}

namespace parse {

const char* lexSwitchAndPatternTable(const char* sourcePosition, SimpleTokenBuffer<LexerToken>& output) {

    LexerToken tok;

    for (;;) {
        sourcePosition = skipWhitespace(sourcePosition);

        const char* tokBegin = sourcePosition;
        tok = LexerToken::Invalid;
        auto head = sourcePosition[0];

        switch (head) {
        case '\0':
            output.tokens.push_back({ LexerToken::EOS, locationInCurrentLine(sourcePosition, output) });
            return sourcePosition;
        case '\n':
            sourcePosition += 1;
            output.addLine(sourcePosition);
            continue;
        case '\r':
            if (sourcePosition[1] == '\n')
                sourcePosition += 2;
            else
                sourcePosition += 1;
            output.addLine(sourcePosition);
            continue;
        case '(':
        case ')':
        case ',':
        case '.':
        case ';':
        case ':':
        case '!':
        case '%':
        case '*':
        case '=':
        case '+':
        case '-':
        case '&':
        case '<':
        case '>': {
            bool repeat = sourcePosition[1] == head;
            bool equal = sourcePosition[1 + (repeat ? 1 : 0)] == '=';
            bool arrow = sourcePosition[1 + (repeat ? 1 : 0) + (equal ? 1 : 0)] == (head == '/' ? '*' : '>');
            int_t advance = 0;
            std::tie(tok, advance) = lookup_table1::lookup(head, repeat, equal, arrow);
            VERIFY(tok != LexerToken::Invalid);
            sourcePosition += advance;
            break;
        }
        case '[':
        case ']':
        case '{':
        case '}':
        case '~':
            tok = lookup_table2::lookup(head);
            sourcePosition += 1;
            break;
        case '|': {
            bool repeat = sourcePosition[1] == '|';
            bool equal = sourcePosition[1 + (repeat ? 1 : 0)] == '=';
            int_t advance = 0;
            std::tie(tok, advance) = repeat
                ? (equal ? std::make_tuple(LexerToken::VertVertEqual, 3) : std::make_tuple(LexerToken::VertVert, 2))
                : (equal ? std::make_tuple(LexerToken::VertEqual, 2) : std::make_tuple(LexerToken::Vert, 1));
            sourcePosition += advance;
            break;
        }
        case '^': {
            int_t advance = 0;
            std::tie(tok, advance) = sourcePosition[1] == '='
                ? std::make_tuple(LexerToken::HatEqual, 2)
                : std::make_tuple(LexerToken::Hat, 1);
            sourcePosition += advance;
            break;
        }
        case '/':
            if (sourcePosition[1] == '*') {
                sourcePosition = skipToEndOfBlockComment(sourcePosition + 2);
                if (sourcePosition[0] == '\0') [[unlikely]] {
                    VERIFY_NOT_REACHED();
                }
                sourcePosition += 2;
                output.whitespace.push_back({ { WhitespaceKind::BlockComment, locationInCurrentLine(tokBegin, output) }, uint32_t(sourcePosition - tokBegin) });
                continue;
            } else if (sourcePosition[1] == '/') {
                sourcePosition = skipToEndOfLine(sourcePosition + 2);
                output.whitespace.push_back({ { WhitespaceKind::LineComment, locationInCurrentLine(tokBegin, output) }, uint32_t(sourcePosition - tokBegin) });
                continue;
            } else if (sourcePosition[1] == '=') {
                tok = LexerToken::SlashEqual;
                sourcePosition += 2;
            } else {
                tok = LexerToken::Slash;
                sourcePosition += 1;
            }
            break;
            // clang-format off
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            // TODO: implement parsing num literals
            tok = LexerToken::NumericLiteral;
            for (;;) {
                char c = sourcePosition[0];
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')
                    sourcePosition += 1;
                else
                    break;
            }
            break;
        case '\'':
            tok = LexerToken::CharacterLiteral;
            sourcePosition = skipToEndOfCharacterLiteral(sourcePosition);
            VERIFY(sourcePosition[0] == '\'');
            sourcePosition += 1;
            break;
        case '\"':
            tok = LexerToken::StringLiteral;
            sourcePosition = skipToEndOfStringLiteral(sourcePosition);
            VERIFY(sourcePosition[0] == '\"');
            sourcePosition += 1;
            break;
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
        case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M':
        case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
        case '_': case '$': case '#': {
            auto [end, token] = readWord(sourcePosition, parse::NoOutput());
            tok = token;
            sourcePosition = end;
            break;
        }
            // clang-format on
        default:

            VERIFY_NOT_REACHED();
        }
        output.tokens.push_back({ tok, locationInCurrentLine(tokBegin, output) });
    }
}

}