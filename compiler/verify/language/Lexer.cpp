#include <verify/language/Lexer.h>

#include <format>

namespace verify::language {

static const char* skipSpaces(const char* position) {
    while (position[0] == ' ')
        position += 1;
    return position;
}

static bool isBulkNameCharacter(char c) {
    return ('A' <= c && c <= 'Z')
        || ('a' <= c && c <= 'z')
        || ('0' <= c && c <= '9')
        || c == '_';
}

//! Every byte of a character but the first one, which are the ones a column does not count
static bool isUtf8ContinuationByte(char c) {
    return ((uint8_t)c & 0xC0) == 0x80;
}

//! The length of the character beginning with 'lead', or 0 if no character begins there
static int_t utf8CharacterLength(char lead) {
    if (((uint8_t)lead & 0x80) == 0x00)
        return 1;
    if (((uint8_t)lead & 0xE0) == 0xC0)
        return 2;
    if (((uint8_t)lead & 0xF0) == 0xE0)
        return 3;
    if (((uint8_t)lead & 0xF8) == 0xF0)
        return 4;
    return 0;
}

//! Consumes the symbol if the source spells it out at the position
/*!
The source is terminated, and the comparison stops at the first byte that differs, so a
character the end of the source cut short is never read past.
*/
static bool matchSymbol(const char*& position, std::string_view symbol) {
    for (int_t i = 0; i < (int_t)symbol.size(); i++) {
        if (position[i] != symbol[i])
            return false;
    }
    position += symbol.size();
    return true;
}

struct Lexer {
    explicit Lexer(LexedFile& file)
        : wordTable(file.wordTable)
        , tokens(file.tokens) { }

    void lex(const char* source);
    Word readWord(const char*& position);
    void lexSymbol(const char*& position);
    [[noreturn]] void error(const char* position, std::string message) const;

    struct ScopeStackEntry {
        uint32_t indent = 0;
    };

    WordStringTable& wordTable;
    std::vector<Token>& tokens;
    std::vector<ScopeStackEntry> scopeStack;
    const char* sourceBegin = nullptr;
};

void Lexer::lex(const char* source) {
    sourceBegin = source;
    const char* position = source;

    // A file may begin with a byte order mark, which says nothing a utf8 source does not
    matchSymbol(position, "\xEF\xBB\xBF");

    scopeStack.push_back({ .indent = 0 });
    tokens.push_back({ TokenKind::BeginScope });

    for (;;) {
        position = skipSpaces(position);
        switch (position[0]) {
        case '\n': {
            position += 1;
            break;
        }
        case '\r': {
            if (position[1] == '\n')
                position += 2;
            else
                position += 1;
            break;
        }
        case '\0':
            break;

        case '(':
            position += 1;
            tokens.push_back({ TokenKind::LeftParen });
            continue;
        case ')':
            position += 1;
            tokens.push_back({ TokenKind::RightParen });
            continue;
        case ':':
            position += 1;
            tokens.push_back({ TokenKind::Colon });
            continue;
        case ',':
            position += 1;
            tokens.push_back({ TokenKind::Comma });
            continue;
        case '.':
            position += 1;
            tokens.push_back({ TokenKind::Point });
            continue;
        case '!':
            if (position[1] == '=') {
                position += 2;
                tokens.push_back({ TokenKind::ExclaimEqual });
                continue;
            } else {
                position += 1;
                tokens.push_back({ TokenKind::Exclaim });
                continue;
            }
        case '<':
            if (position[1] == '-') {
                position += 2;
                tokens.push_back({ TokenKind::LeftArrow });
                continue;
            } else {
                error(position, "Expected '-' after '<'");
            }
        case '=':
            position += 1;
            tokens.push_back({ TokenKind::Equal });
            continue;

        case '$':
            position += 1;
            tokens.push_back({ TokenKind::LocalName, readWord(position).toUint() });
            continue;
        case '%':
            position += 1;
            tokens.push_back({ TokenKind::TheoremName, readWord(position).toUint() });
            continue;
        case '@':
            position += 1;
            tokens.push_back({ TokenKind::LabelName, readWord(position).toUint() });
            continue;
        case '#':
            position += 1;
            tokens.push_back({ TokenKind::GlobalName, readWord(position).toUint() });
            continue;
            // clang-format off
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M':
    case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
    case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
    case '_':
            // clang-format on
            tokens.push_back({ TokenKind::Identifier, readWord(position).toUint() });
            continue;
            // clang-format off
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            // clang-format on
            error(position, "Numbers are not supported yet");

        default:
            // Everything outside of ascii is a symbol, names are spelled with ascii alone
            if ((uint8_t)position[0] >= 0x80) {
                lexSymbol(position);
                continue;
            }
            error(position, std::format("Unexpected character '{}'", position[0]));
        }

        // Handle scope changes
        const char* lineBegin;
        std::vector<Word> labels;
        for (;;) {
            lineBegin = position;
            if (position[0] == '@') {
                position += 1;
                labels.push_back(readWord(position));
                if (position[0] != ':')
                    error(position, "Expected ':' after label");
                position += 1;
            }
            position = skipSpaces(position);
            if (position[0] == '\r') {
                if (position[1] == '\n')
                    position += 2;
                else
                    position += 1;
                continue;
            } else if (position[0] == '\n') {
                position += 1;
                continue;
            }
            break;
        }
        uint32_t indent = position - lineBegin;
        if (position[0] == '\0') {
            for ([[maybe_unused]] auto& entry : scopeStack)
                tokens.push_back({ TokenKind::EndScope });
            scopeStack.clear();
            return;
        }
        if (indent > scopeStack.back().indent) {
            tokens.push_back({ TokenKind::BeginScope });
            scopeStack.push_back({ .indent = indent });
            for (Word label : labels)
                tokens.push_back({ TokenKind::ContinueScope, label.toUint() });
        } else {
            while (indent < scopeStack.back().indent) {
                tokens.push_back({ TokenKind::EndScope });
                scopeStack.pop_back();
            }
            if (indent != scopeStack.back().indent)
                error(position, "Line is not indented to any scope it could continue");
            if (labels.empty()) {
                tokens.push_back({ TokenKind::ContinueScope });
            } else {
                for (Word label : labels)
                    tokens.push_back({ TokenKind::ContinueScope, label.toUint() });
            }
        }
    }
}

[[gnu::always_inline]] Word Lexer::readWord(const char*& position) {
    const char* begin = position;
    while (isBulkNameCharacter(*position))
        position += 1;
    return wordTable.get(std::string_view(begin, position));
}

//! A symbol reads as the token the operator it stands for is written with
void Lexer::lexSymbol(const char*& position) {
    const char* begin = position;
    if (matchSymbol(position, symbols::AND)) {
        tokens.push_back({ TokenKind::Identifier, words["and"].toUint() });
        return;
    }
    if (matchSymbol(position, symbols::OR)) {
        tokens.push_back({ TokenKind::Identifier, words["or"].toUint() });
        return;
    }
    if (matchSymbol(position, symbols::NOT)) {
        tokens.push_back({ TokenKind::Exclaim });
        return;
    }
    if (matchSymbol(position, symbols::NOT_EQUAL)) {
        tokens.push_back({ TokenKind::ExclaimEqual });
        return;
    }
    if (matchSymbol(position, symbols::UNION)) {
        tokens.push_back({ TokenKind::Identifier, words["union"].toUint() });
        return;
    }
    if (matchSymbol(position, symbols::INTERSECTION)) {
        tokens.push_back({ TokenKind::Identifier, words["intersection"].toUint() });
        return;
    }
    if (matchSymbol(position, symbols::SET_MINUS)) {
        tokens.push_back({ TokenKind::Identifier, words["setminus"].toUint() });
        return;
    }

    // The character stands for nothing here, if it is a character at all. Its bytes are read
    // one at a time, so a character the end of the source cut short ends the character too.
    int_t length = utf8CharacterLength(begin[0]);
    for (int_t i = 1; i < length; i++) {
        if (!isUtf8ContinuationByte(begin[i]))
            error(begin, "Source is not encoded as utf8");
    }
    if (length == 0)
        error(begin, "Source is not encoded as utf8");
    error(begin, std::format("Unknown operator '{}'", std::string_view(begin, length)));
}

[[noreturn]] void Lexer::error(const char* position, std::string message) const {
    int_t line = 1;
    const char* lineBegin = sourceBegin;
    for (const char* it = sourceBegin; it < position; it++) {
        if (*it == '\n') {
            line += 1;
            lineBegin = it + 1;
        }
    }
    // A column counts characters, of which the bytes that continue one are a part
    int_t column = 1;
    for (const char* it = lineBegin; it < position; it++) {
        if (!isUtf8ContinuationByte(*it))
            column += 1;
    }
    throw ParserException(std::format("{}:{}: {}", line, column, message));
}

LexedFile lexFile(const char* source) {
    LexedFile file;
    Lexer lexer { file };
    lexer.lex(source);
    return file;
}

}
