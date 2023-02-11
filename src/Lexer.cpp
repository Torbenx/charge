#include "Lexer.h"
#include <array>

const char* toShortString(TokenKind kind) {
    using enum TokenKind;
    // clang-format off
    switch (kind) {
    case Invalid: return "xxxx"; 
    case LeftParen: return "(";
    case RightParen: return ")";
    case LeftAngle: return "[";
    case RightAngle: return "]";
    case LeftBrace: return "{";
    case RightBrace: return "}";
    case Tilde: return "~";
    case PlusPlus: return "++";
    case MinusMinus: return "--";
    case Exclaim: return "!";
    case Plus: return "+";
    case Minus: return "-";
    case ExclaimEqual: return "!=";
    case PlusEqual: return "+=";
    case MinusEqual: return "-=";
    case AmpAmp: return "&&";
    case Hat: return "^";
    case Percent: return "%";
    case Amp: return "&";
    case HatEqual: return "^=";
    case PercentEqual: return "%=";
    case AmpEqual: return "&=";
    case VertVert: return "||";
    case Slash: return "/";
    case Star: return "*";
    case Vert: return "|";
    case SlashEqual: return "/=";
    case StarEqual: return "*=";
    case VertEqual: return "|=";
    case Equal: return "=";
    case EqualEqual: return "==";
    case Less: return "<";
    case LessLess: return "<<";
    case LessEqual: return "<=";
    case LessLessEqual: return "<<=";
    case Greater: return ">";
    case GreaterGreater: return ">>";
    case GreaterEqual: return ">=";
    case GreaterGreaterEqual: return ">>=";
    case Comma: return ",";
    case Point: return ".";
    case Colon: return ":";
    case SemiColon: return ";";
    case Word: return "word";
    default: return "????";
    }
    // clang-format on
}

namespace {

using u8 = uint8_t;

template<typename T>
struct CharacterTable {
    struct Entry {
        u8 first;
        u8 last;
        T value;
        constexpr Entry(u8 first, u8 last, T value)
            : first(first), last(last), value(value) { }
        constexpr Entry(u8 c, T value)
            : first(c), last(c), value(value) { }
    };
    std::array<T, 128> table = {};
    constexpr CharacterTable(T defaultValue, std::initializer_list<Entry> entries) {
        table.fill(defaultValue);
        for (Entry e : entries) {
            for (u8 c = e.first; c <= e.last; c++)
                table[c] = e.value;
        }
    }

    constexpr T operator[](u8 c) const {
        return table[c];
    }
};

struct HeadInfo {
    enum PunctuationMode {
        SingleCharacterOnly, // ()[]{}~,.;
        SubsequentEqual, // ^!%/*=
        RepeatOnceOrSubsequentEqual, // +-&|
        RepeatOnceAndSubsequentEqual, // <>
    };
    static_assert((int)TokenKind::COUNT < 64);
    uint8_t m_kind : 6 = 0;
    uint8_t mode : 2 = 0;
    constexpr HeadInfo() = default;
    constexpr HeadInfo(TokenKind kind, PunctuationMode mode)
        : m_kind(std::to_underlying(kind)), mode(mode) { }

    TokenKind kind() const { return (TokenKind)m_kind; }
    PunctuationMode punctuationMode() const { return (PunctuationMode)mode; }
};

struct HeadInfoTable {
    using enum TokenKind;
    using enum HeadInfo::PunctuationMode;
    static constexpr CharacterTable<HeadInfo> table {
        { Invalid, /*does not apply*/ SingleCharacterOnly },
        {
            { '(', { LeftParen, SingleCharacterOnly } },
            { ')', { RightParen, SingleCharacterOnly } },
            { '[', { LeftAngle, SingleCharacterOnly } },
            { ']', { RightAngle, SingleCharacterOnly } },
            { '{', { LeftBrace, SingleCharacterOnly } },
            { '}', { RightBrace, SingleCharacterOnly } },
            { '~', { Tilde, SingleCharacterOnly } },
            { ',', { Comma, SingleCharacterOnly } },
            { '.', { Point, SingleCharacterOnly } },
            { ':', { Colon, SingleCharacterOnly } },
            { ';', { SemiColon, SingleCharacterOnly } },

            { '^', { Hat, SubsequentEqual } },
            { '!', { Exclaim, SubsequentEqual } },
            { '%', { Percent, SubsequentEqual } },
            { '/', { Slash, SubsequentEqual } },
            { '*', { Star, SubsequentEqual } },
            { '=', { Equal, SubsequentEqual } },

            { '+', { Plus, RepeatOnceOrSubsequentEqual } },
            { '-', { Minus, RepeatOnceOrSubsequentEqual } },
            { '&', { Amp, RepeatOnceOrSubsequentEqual } },
            { '|', { Vert, RepeatOnceOrSubsequentEqual } },

            { '<', { Less, RepeatOnceAndSubsequentEqual } },
            { '>', { Greater, RepeatOnceAndSubsequentEqual } },

            { 'a', 'z', { Word, /*does not apply*/ SingleCharacterOnly } },
            { 'A', 'Z', { Word, /*does not apply*/ SingleCharacterOnly } },
            { '_', { Word, /*does not apply*/ SingleCharacterOnly } },
            { '$', { Word, /*does not apply*/ SingleCharacterOnly } },
        },
    };
};

constexpr bool isLetter(u8 c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

constexpr bool isFirstWordChar(u8 c) {
    return isLetter(c) || c == '_' || c == '$';
}
constexpr bool isNumber(u8 c) {
    return c >= '0' && c <= '9';
}
constexpr bool isBulkWordChar(u8 c) {
    return isFirstWordChar(c) || isNumber(c);
}
constexpr bool isWhiteSpace(u8 c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

}

Lexer::Lexer(SourceBuffer buffer, bool dump)
    : buffer(buffer), dumpTokens(dump) {
    uint32_t firstToken = nextNonWhiteSpace(0);
    m_position = firstToken;
    tok.flags |= firstToken > 0 ? TokenFlags::HasLeadingWhiteSpace : 0;
}

uint32_t Lexer::nextNonWhiteSpace(uint32_t pos) const {
    while (isWhiteSpace(buffer[pos])) {
        pos += 1;
    }
    return pos;
}

void Lexer::advance() {
    auto makeInvalidToken = [this]() {
        tok = {
            .start = pos(),
            .length = 0,
            .m_kind = std::to_underlying(TokenKind::Invalid),
            .flags = 0,
        };
        if (dumpTokens)
            fmt::println("making invalid token");
    };

    u8 head = buffer[pos()];
    if (head >= 128) [[unlikely]] {
        return makeInvalidToken();
    }
    auto headInfo = HeadInfoTable::table[head];

    uint32_t end = pos() + 1;
    uint32_t kind = std::to_underlying(headInfo.kind());
    if ((TokenKind)kind == TokenKind::Invalid) [[unlikely]] {
        return makeInvalidToken();
    }

    if ((TokenKind)kind == TokenKind::Word) {
        while (isBulkWordChar(buffer[end])) {
            end += 1;
        }
    } else {
        using enum HeadInfo::PunctuationMode;
        switch (headInfo.punctuationMode()) {
        case SingleCharacterOnly: {
            break;
        }
        case SubsequentEqual: {
            bool hasEqual = buffer[pos() + 1] == '=';
            kind += hasEqual ? 3 : 0;
            end += hasEqual ? 1 : 0;
            break;
        }
        case RepeatOnceOrSubsequentEqual: {
            bool hasEqual = buffer[pos() + 1] == '=';
            kind += hasEqual ? 3 : 0;
            end += hasEqual ? 1 : 0;

            bool hasRepeat = buffer[pos() + 1] == head;
            kind -= hasRepeat ? 3 : 0;
            end += hasRepeat ? 1 : 0;
            break;
        }
        case RepeatOnceAndSubsequentEqual: {
            bool hasRepeat = buffer[pos() + 1] == head;
            kind += hasRepeat ? 1 : 0;
            end += hasRepeat ? 1 : 0;

            bool hasEqual = buffer[end] == '=';
            kind += hasEqual ? 2 : 0;
            end += hasEqual ? 1 : 0;
            break;
        }
        }
    }

    bool leadingSpace = tok.hasTrailingWhiteSpace();
    uint32_t flags = leadingSpace ? TokenFlags::HasLeadingWhiteSpace : 0;

    uint32_t spaceEnd = nextNonWhiteSpace(end);
    flags |= (spaceEnd == end) ? TokenFlags::HasTrailingWhiteSpace : 0;

    tok = {
        .start = pos(),
        .length = end - pos(),
        .m_kind = kind,
        .flags = flags,
    };
    m_position = spaceEnd;
    if (dumpTokens)
        fmt::println("'{:4}': \"{}\"", toShortString(tok.kind()), buffer.sourceCode(tok));
}

void dumpTokens(Lexer& lex) {
    do {
        lex.advance();
        fmt::println("'{:4}': \"{}\"", toShortString(lex.tok.kind()), lex.buffer.sourceCode(lex.tok));
    } while (lex.tok.kind() != TokenKind::Invalid);
}