namespace parse::first_table {

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
    VERIFY(character < 0x80);
    return character;
}

static constexpr auto makeTable(std::initializer_list<Input> inputs) {
    std::array<Output, 0x80> output = {};
    for (auto input : inputs) {
        for (uint8_t c = input.first; c <= input.last; c++) {
            output[getOffset(c)] = Output { input.token };
        }
    }
    return output;
}

static constexpr auto table = makeTable({
    { 'a', 'z', LexerToken::Identifier },
    { 'A', 'Z', LexerToken::Identifier },
    { '_', LexerToken::Identifier },
    { '$', LexerToken::Identifier },
    { '#', LexerToken::Identifier },

    { '[', LexerToken::LeftSquare },
    { ']', LexerToken::RightSquare },
    { '{', LexerToken::LeftBrace },
    { '}', LexerToken::RightBrace },
    { '~', LexerToken::Tilde },
    { '(', LexerToken::LeftParen },
    { ')', LexerToken::RightParen },
    { ',', LexerToken::Comma },
    { '.', LexerToken::Point },
    { ';', LexerToken::SemiColon },

    { ':', LexerToken::Colon },

    { '!', LexerToken::Exclaim },
    { '%', LexerToken::Percent },
    { '*', LexerToken::Star },
    { '^', LexerToken::Hat },

    { '/', LexerToken::Slash },

    { '=', LexerToken::Equal },

    { '+', LexerToken::Plus },
    { '-', LexerToken::Minus },

    { '&', LexerToken::Amp },
    { '|', LexerToken::Vert },

    { '<', LexerToken::Less },
    { '>', LexerToken::Greater },

    { '0', '9', LexerToken::NumericLiteral },
    { '\'', LexerToken::CharacterLiteral },
    { '\"', LexerToken::StringLiteral },
});

static constexpr LexerToken lookup(uint8_t character) { return table[getOffset(character)].token; }

}

namespace parse {

const char* lexTableHybrid(const char* sourcePosition, SimpleTokenBuffer<LexerToken>& output) {

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
        case ';':
            tok = first_table::lookup(head);
            sourcePosition += 1;
            break;
        case ':':
            if (sourcePosition[1] == ':') {
                tok = LexerToken::ColonColon;
                sourcePosition += 2;
            } else {
                sourcePosition += 1;
            }
            break;
        case '!':
        case '%':
        case '*':
        case '^':
            tok = first_table::lookup(head);
            if (sourcePosition[1] == '=') {
                tok = LexerToken(std::to_underlying(tok) - std::to_underlying(LexerToken::Star) + std::to_underlying(LexerToken::StarEqual));
                sourcePosition += 2;
            } else {
                sourcePosition += 1;
            }
            break;
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
                sourcePosition += 1;
            }
            break;
        case '=':
            if (sourcePosition[1] == '=') {
                tok = LexerToken::EqualEqual;
                sourcePosition += 2;
            } else if (sourcePosition[1] == '>') {
                tok = LexerToken::EqualGreater;
                sourcePosition += 2;
            } else {
                sourcePosition += 1;
            }
            break;
        case '+':
            if (sourcePosition[1] == '=') {
                tok = LexerToken::PlusEqual;
                sourcePosition += 2;
            } else if (sourcePosition[1] == '+') {
                tok = LexerToken::PlusPlus;
                sourcePosition += 2;
            } else {
                sourcePosition += 1;
            }
            break;
        case '-':
            if (sourcePosition[1] == '=') {
                tok = LexerToken::MinusEqual;
                sourcePosition += 2;
            } else if (sourcePosition[1] == '-') {
                tok = LexerToken::MinusMinus;
                sourcePosition += 2;
            } else if (sourcePosition[1] == '>') {
                tok = LexerToken::MinusGreater;
                sourcePosition += 2;
            } else {
                sourcePosition += 1;
            }
            break;
        case '&':
        case '|':
        case '<':
        case '>':
            tok = first_table::lookup(head);
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