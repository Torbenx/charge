#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <string_view>

void verify_failed(const char* condStr, const char* file, const char* func, int line) {
    fmt::println("VERIFY failed {}:{}: {}(): {}", file, line, func, condStr);
}

constexpr bool verify(bool cond, const char* condStr, const char* file, const char* func, int line) {
    if (!cond)
        verify_failed(condStr, file, func, line);
    return cond;
}
template<typename L, typename R>
constexpr bool expect_eq(const L& lhs, const R& rhs, const char* lhsStr, const char* rhsStr, const char* file, const char* func, int line) {
    bool b = lhs != rhs;
    if (b)
        fmt::println("EXPECT failed {}:{}: {}(): {} {{{}}} == {} {{{}}}", file, line, func, lhsStr, lhs, rhsStr, rhs);
    return b;
}

#define VERIFY(cond) verify(cond, #cond, __FILE__, __func__, __LINE__)
#define EXPECT_EQ(lhs, rhs) expect_eq(lhs, rhs, #lhs, #rhs, __FILE__, __func__, __LINE__)

enum TokenKind : uint32_t {
    Invalid,
    LeftParen,
    RightParen, // ()
    LeftAngle,
    RightAngle, // []
    LeftBrace,
    RightBrace, // {}

    // begin unary
    Tilde, // ~

    PlusPlus, // ++
    MinusMinus, // --
    Exclaim, // !
    // begin binary
    Plus, // +
    Minus, // -
    // end unary
    ExclaimEqual, // !=
    PlusEqual, // +=
    MinusEqual, // -=

    AmpAmp, // &&
    Hat, // ^
    Percent, // %
    Amp, // &
    HatEqual, // ^=
    PercentEqual, // %=
    AmpEqual, // &=

    VertVert, // ||
    Slash, // /
    Star, // *
    Vert, // |
    SlashEqual, // /=
    StarEqual, // *=
    VertEqual, // |=

    Equal, // =
    DummyOp1,
    DummyOp2,
    EqualEqual, // ==

    Less, // <
    LessLess, // <<
    LessEqual, // <=
    LessLessEqual, // <<=
    Greater, // >
    GreaterGreater, // >>
    GreaterEqual, // >=
    GreaterGreaterEqual, // >>=
    // end binary

    FirstUnaryOp = Tilde,
    FirstBinaryOp = Plus,
    LastUnaryOp = Minus,
    LastBinaryOp = GreaterGreaterEqual,
    FirstOperator = FirstUnaryOp,
    LastOperator = LastBinaryOp,

    Comma, // ,
    Point, // .
    Colon, // :
    SemiColon, // ;
    Word, // abc123
    COUNT,
};

constexpr const char* toShortString(TokenKind kind) {
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
template<>
struct fmt::formatter<TokenKind> : formatter<string_view> {
    template<typename FormatContext>
    auto format(TokenKind kind, FormatContext& ctx) const {
        return formatter<string_view>::format(toShortString(kind), ctx);
    }
};

constexpr bool isOperator(TokenKind kind) {
    return kind >= TokenKind::FirstOperator && kind <= TokenKind::LastBinaryOp;
}

constexpr bool isUnaryOp(TokenKind kind) {
    return kind >= TokenKind::FirstUnaryOp && kind <= TokenKind::LastUnaryOp;
}

constexpr bool isBinaryOp(TokenKind kind) {
    return kind >= TokenKind::FirstBinaryOp && kind <= TokenKind::LastBinaryOp;
}

constexpr bool isGoodToken(TokenKind kind) {
    return kind >= 1 && kind < TokenKind::COUNT && kind != TokenKind::DummyOp1 && kind != TokenKind::DummyOp2;
}

enum TokenFlags : uint32_t {
    HasLeadingWhiteSpace = 0x01,
    HasTrailingWhiteSpace = 0x02,
};

struct Token {
    static constexpr int KIND_BITS = 8;
    static_assert((1 << KIND_BITS) > TokenKind::COUNT);

    uint64_t start : 32 = 0;
    uint64_t length : 8 = 0;
    uint64_t kind : KIND_BITS = 0;
    uint64_t flags : 16 = 0;

    bool hasTrailingWhiteSpace() const {
        return flags & HasTrailingWhiteSpace;
    }
};

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
    static_assert(TokenKind::COUNT < 64);
    uint8_t m_kind : 6 = 0;
    uint8_t mode : 2 = 0;
    constexpr HeadInfo() = default;
    constexpr HeadInfo(TokenKind kind, PunctuationMode mode)
        : m_kind(kind), mode(mode) { }

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

struct Lexer {
    const u8* buffer = nullptr;
    uint32_t m_position = 0;
    uint32_t pos() const { return m_position; }

    Lexer(const u8* buffer);

    void advance();
    uint32_t nextNonWhiteSpace(uint32_t pos) const;

    Token lastToken = {};
    Token currentToken() const { return lastToken; }

    std::string_view sourceCode(Token t) const {
        return { (const char*)(buffer + t.start), t.length };
    }
};

Lexer::Lexer(const u8* buffer)
    : buffer(buffer) {
    uint32_t firstToken = nextNonWhiteSpace(0);
    m_position = firstToken;
    lastToken.flags |= firstToken > 0 ? TokenFlags::HasLeadingWhiteSpace : 0;
}

uint32_t Lexer::nextNonWhiteSpace(uint32_t pos) const {
    while (isWhiteSpace(buffer[pos])) {
        pos += 1;
    }
    return pos;
}

void Lexer::advance() {
    auto makeInvalidToken = [this]() {
        lastToken = {
            .start = pos(),
            .length = 0,
            .kind = TokenKind::Invalid,
            .flags = 0,
        };
    };

    u8 head = buffer[pos()];
    if (head >= 128) [[unlikely]] {
        return makeInvalidToken();
    }
    auto headInfo = HeadInfoTable::table[head];

    uint32_t end = pos() + 1;
    uint32_t kind = headInfo.kind();
    if (kind == TokenKind::Invalid) [[unlikely]] {
        return makeInvalidToken();
    }

    if (kind == TokenKind::Word) {
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

    bool leadingSpace = lastToken.hasTrailingWhiteSpace();
    uint32_t flags = leadingSpace ? TokenFlags::HasLeadingWhiteSpace : 0;

    uint32_t spaceEnd = nextNonWhiteSpace(end);
    flags |= (spaceEnd == end) ? TokenFlags::HasTrailingWhiteSpace : 0;

    lastToken = {
        .start = pos(),
        .length = end - pos(),
        .kind = kind,
        .flags = flags,
    };
    m_position = spaceEnd;
}

void dumpTokens(Lexer& lex) {
    while (true) {
        lex.advance();
        Token t = lex.currentToken();
        fmt::println("'{:4}': \"{}\"", toShortString((TokenKind)t.kind), lex.sourceCode(t));
        if (t.kind == TokenKind::Invalid)
            break;
    }
}

void test() {
    auto testSingleToken = [](TokenKind kind) {
        Lexer lex((const u8*)toShortString(kind));
        lex.advance();
        EXPECT_EQ((TokenKind)lex.currentToken().kind, kind);
        lex.advance();
        EXPECT_EQ((TokenKind)lex.currentToken().kind, TokenKind::Invalid);
    };

    for (uint32_t kind = 1; kind < TokenKind::COUNT; kind++) {
        if (isGoodToken((TokenKind)kind))
            testSingleToken((TokenKind)kind);
    }
}

int main() {
    test();

    Lexer lex((const u8*)"ö");
    lex.advance();
    EXPECT_EQ((TokenKind)lex.currentToken().kind, TokenKind::Invalid);
}