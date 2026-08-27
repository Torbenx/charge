namespace parse::char_table {

template<typename T>
struct Table {
    struct Input {
        uint8_t first;
        uint8_t last;
        T value;
        constexpr Input(uint8_t character, T value)
            : first(character), last(character), value(value) { }
        constexpr Input(uint8_t first, uint8_t last, T value)
            : first(first), last(last), value(value) { }
    };

    constexpr Table(T defaultValue, std::initializer_list<Input> inputs) {
        entries.fill(defaultValue);
        for (Input input : inputs) {
            for (uint8_t c = input.first; c <= input.last; c++) {
                entries[c] = input.value;
            }
        }
    }

    constexpr T operator()(uint8_t c) const { return entries[c]; }

    std::array<T, 0x80> entries;
};

template<typename T, int_t N>
struct MultiTable {
    constexpr MultiTable(const Table<T> (&in)[N]) {
        for (int_t i = 0; i < N; i++) {
            std::copy_n(in[i].entries.data(), 0x80, entries.data() + i * 0x80);
        }
    }

    constexpr T operator()(uint8_t tbl, uint8_t c) const { return entries[tbl * 0x80 + c]; }

    std::array<T, 0x80 * N> entries;
};

}

namespace parse {

const char* lexSwitchAndTable(const char* sourcePosition, SimpleTokenBuffer<LexerToken>& output) {
    using namespace char_table;
    struct FirstCharEntry {
        LexerToken token;
        uint8_t secondCharTableIndex;
    };
    struct SecondCharEntry {
        constexpr SecondCharEntry()
            : tokenOffset(0), advance(1) { }
        constexpr SecondCharEntry(uint8_t offset)
            : tokenOffset(offset), advance(2) { }
        uint8_t tokenOffset;
        uint8_t advance;
    };
    static constexpr Table<SecondCharEntry> nullTable = { SecondCharEntry(), {} };
    static constexpr Table<SecondCharEntry> offsetFollowedByEqual = {
        SecondCharEntry(),
        { { '=', std::to_underlying(LexerToken::PlusEqual) - std::to_underlying(LexerToken::Plus) } },
    };
    static constexpr Table<SecondCharEntry> offsetPlus = {
        SecondCharEntry(),
        {
            { '+', std::to_underlying(LexerToken::PlusPlus) - std::to_underlying(LexerToken::Plus) },
            { '=', std::to_underlying(LexerToken::PlusEqual) - std::to_underlying(LexerToken::Plus) },
        },
    };
    static constexpr Table<SecondCharEntry> offsetMinus = {
        SecondCharEntry(),
        {
            { '-', std::to_underlying(LexerToken::MinusMinus) - std::to_underlying(LexerToken::Minus) },
            { '=', std::to_underlying(LexerToken::MinusEqual) - std::to_underlying(LexerToken::Minus) },
            { '>', std::to_underlying(LexerToken::MinusGreater) - std::to_underlying(LexerToken::Minus) },
        },
    };
    static constexpr Table<SecondCharEntry> offsetEqual = {
        SecondCharEntry(),
        {
            { '=', std::to_underlying(LexerToken::EqualEqual) - std::to_underlying(LexerToken::Equal) },
            { '>', std::to_underlying(LexerToken::EqualGreater) - std::to_underlying(LexerToken::Equal) },
        },
    };
    static constexpr Table<SecondCharEntry> offsetColon = {
        SecondCharEntry(),
        {
            { ':', std::to_underlying(LexerToken::ColonColon) - std::to_underlying(LexerToken::Colon) },
        },
    };
    static constexpr MultiTable offsetTables({ nullTable, offsetFollowedByEqual, offsetPlus, offsetMinus, offsetEqual, offsetColon });

    static constexpr Table<FirstCharEntry> charToken = {
        { LexerToken::Invalid, 0 },
        {
            { '[', { LexerToken::LeftSquare, 0 } },
            { ']', { LexerToken::RightSquare, 0 } },
            { '{', { LexerToken::LeftBrace, 0 } },
            { '}', { LexerToken::RightBrace, 0 } },
            { '~', { LexerToken::Tilde, 0 } },
            { '(', { LexerToken::LeftParen, 0 } },
            { ')', { LexerToken::RightParen, 0 } },
            { ',', { LexerToken::Comma, 0 } },
            { '.', { LexerToken::Point, 0 } },
            { ';', { LexerToken::SemiColon, 0 } },

            { ':', { LexerToken::Colon, 5 } },

            { '/', { LexerToken::Slash, limits::max } },

            { '!', { LexerToken::Exclaim, 1 } },
            { '%', { LexerToken::Percent, 1 } },
            { '*', { LexerToken::Star, 1 } },
            { '^', { LexerToken::Hat, 1 } },

            { '=', { LexerToken::Equal, 4 } },

            { '+', { LexerToken::Plus, 2 } },
            { '-', { LexerToken::Minus, 3 } },

            { '&', { LexerToken::Amp, limits::max } },
            { '|', { LexerToken::Vert, limits::max } },

            { '<', { LexerToken::Less, limits::max } },
            { '>', { LexerToken::Greater, limits::max } },
        },
    };

    for (;;) {
        sourcePosition = skipWhitespace(sourcePosition);

        const char* tokBegin = sourcePosition;
        auto head = sourcePosition[0];
        LexerToken tok = LexerToken::Invalid;

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
        case '[':
        case ']':
        case '{':
        case '}':
        case '~':
        case '(':
        case ')':
        case ',':
        case '.':
        case ';': {
            auto [baseTok, tbl] = charToken(head);
            tok = baseTok;
            sourcePosition += 1;
            break;
        }
        case '!':
        case '%':
        case '*':
        case '^':
        case '=':
        case '+':
        case '-':
        case ':': {
            auto [baseTok, tbl] = charToken(head);
            auto [offset, advance] = offsetTables(tbl, sourcePosition[1]);
            tok = LexerToken(std::to_underlying(baseTok) + offset);
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
                output.whitespace.push_back({ { WhitespaceKind::LineComment, locationInCurrentLine(tokBegin, output) }, uint32_t(sourcePosition - tokBegin) });
                continue;
            } else if (sourcePosition[1] == '/') {
                sourcePosition = skipToEndOfLine(sourcePosition + 2);
                output.whitespace.push_back({ { WhitespaceKind::BlockComment, locationInCurrentLine(tokBegin, output) }, uint32_t(sourcePosition - tokBegin) });
                continue;
            } else if (sourcePosition[1] == '=') {
                tok = LexerToken::SlashEqual;
                sourcePosition += 2;
            } else {
                sourcePosition += 1;
            }
            break;
        case '&':
        case '|':
        case '<':
        case '>': {
            auto [baseTok, tbl] = charToken(head);
            tok = baseTok;
            if (sourcePosition[1] == head) {
                sourcePosition += 1;
                tok = LexerToken(std::to_underlying(tok) + 2);
            }
            if (sourcePosition[1] == '=') {
                sourcePosition += 1;
                tok = LexerToken(std::to_underlying(tok) + 1);
            }
            sourcePosition += 1;
            break;
            // clang-format off
        }
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
            const char* tokBegin = sourcePosition;
            do {
                sourcePosition += 1;
            } while (isWordBulkCharacter(sourcePosition[0]));
            const auto* entry = KeywordTable::get(tokBegin, sourcePosition - tokBegin);
            tok = entry == nullptr ? LexerToken::Identifier : entry->token;
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