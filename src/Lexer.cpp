#include "Parser.h"
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
    case AmpAmpEqual: return "&&=";
    case Hat: return "^";
    case Percent: return "%";
    case Amp: return "&";
    case HatEqual: return "^=";
    case PercentEqual: return "%=";
    case AmpEqual: return "&=";
    case VertVert: return "||";
    case VertVertEqual: return "||=";
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
    case Question: return "?";
    case Comma: return ",";
    case Point: return ".";
    case Colon: return ":";
    case ColonColon: return "::";
    case SemiColon: return ";";
    case FatArrow: return "=>";
    case Word: return "word";
    case IntegerLiteral: return "int";
    case EOS: return "EOS";
    default: return "????";
    }
    // clang-format on
}
const char* exampleString(TokenKind kind) {
    if (kind == TokenKind::EOS)
        return "";
    if (kind == TokenKind::IntegerLiteral)
        return "123";
    return toShortString(kind);
}

namespace {

using u8 = uint8_t;

struct Table {
    struct Input {
        TokenKind bare;
        TokenKind repeat = bare;
        TokenKind followed = bare;
        TokenKind repeatFollowed = repeat;
        uint8_t bareAdvance = 1;
    };
    struct InputRange : Input {
        char first, last;
        constexpr InputRange(char target, Input input)
            : Input(input), first(target), last(target) { }
        constexpr InputRange(char first, char last, Input input)
            : Input(input), first(first), last(last) { }
    };
    struct Output {
        uint8_t kind : 6 = 0;
        uint8_t advance : 2 = 0;
        constexpr Output() = default;
        constexpr Output(TokenKind kind, int advance)
            : kind((uint8_t)kind), advance(advance) { }
    };
    std::array<Output, 0x60 * 4> table;
    constexpr Table(std::initializer_list<InputRange> inputs) {
        for (auto input : inputs) {
            for (char c = input.first; c <= input.last; c++) {
                table[c - 0x20 + 0x60 * 0] = { input.bare, input.bareAdvance };
                table[c - 0x20 + 0x60 * 1] = { input.repeat, input.repeat != input.bare ? 2 : input.bareAdvance };
                table[c - 0x20 + 0x60 * 2] = { input.followed, input.followed != input.bare ? 2 : input.bareAdvance };
                table[c - 0x20 + 0x60 * 3] = { input.repeatFollowed, input.repeatFollowed != input.repeat ? 3 : (input.repeatFollowed != input.bare ? 2 : input.bareAdvance) };
            }
        }
    }
};

struct TableHolder {
    using enum TokenKind;
    static constexpr Table table {
        { '(', { LeftParen } },
        { ')', { RightParen } },
        { '[', { LeftAngle } },
        { ']', { RightAngle } },
        { '{', { LeftBrace } },
        { '}', { RightBrace } },
        { '~', { Tilde } },
        { '?', { Question } },
        { ',', { Comma } },
        { '.', { Point } },
        { ';', { SemiColon } },
        { ':', { .bare = Colon, .repeat = ColonColon } },

        { '^', { .bare = Hat, .followed = HatEqual } },
        { '!', { .bare = Exclaim, .followed = ExclaimEqual } },
        { '%', { .bare = Percent, .followed = PercentEqual } },
        { '/', { .bare = Slash, .followed = SlashEqual } },
        { '*', { .bare = Star, .followed = StarEqual } },
        { '=', { .bare = Equal, .repeat = EqualEqual, .followed = FatArrow } },

        { '+', { .bare = Plus, .repeat = PlusPlus, .followed = PlusEqual } },
        { '-', { .bare = Minus, .repeat = MinusMinus, .followed = MinusEqual } },
        { '&', { .bare = Amp, .repeat = AmpAmp, .followed = AmpEqual, .repeatFollowed = AmpAmpEqual } },
        { '|', { .bare = Vert, .repeat = VertVert, .followed = VertEqual, .repeatFollowed = VertVertEqual } },

        { '<', { .bare = Less, .repeat = LessLess, .followed = LessEqual, .repeatFollowed = LessLessEqual } },
        { '>', { .bare = Greater, .repeat = GreaterGreater, .followed = GreaterEqual, .repeatFollowed = GreaterGreaterEqual } },

        { '0', '9', { .bare = IntegerLiteral, .bareAdvance = 0 } },

        { 'a', 'z', { Word } },
        { 'A', 'Z', { Word } },
        { '_', { Word } },
        { '$', { Word } },
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
constexpr bool isIntLiteralChar(u8 c) {
    return isLetter(c) || isNumber(c) || c == '\'';
}

}

void Parser::reset(SourceBuffer buffer) {
    source = buffer;
    m_position = 0;
    skipWhiteSpace();
    advance();
}

uint32_t Parser::nextNonWhiteSpace(uint32_t pos) const {
    while (isWhiteSpace(source[pos])) {
        pos += 1;
    }
    return pos;
}

void Parser::advance() {
    u8 head = source[pos()];
    if (head == '\0') [[unlikely]] {
        tok = {
            .m_kind = TokenKind::EOS,
            .beginPosition = pos(),
        };
        return;
    }
    if (head >= 0x80 || head < 0x20) [[unlikely]] {
        VERIFY_NOT_REACHED();
    }

    // x
    // xx
    // x=
    // xx=
    // =>

    uint32_t end = pos() + 1;

    bool repeat = source[end] == head;
    end += repeat ? 1 : 0;

    u8 followup = head == '=' ? '>' : '=';
    bool followed = source[end] == followup;
    uint32_t idx = (uint32_t)head - 0x20 + (repeat ? 0x60 : 0) + (followed ? 0x60 * 2 : 0);
    auto [kind, advance] = TableHolder::table.table[idx];

    end = pos() + advance;
    Word word = {};
    if ((TokenKind)kind == TokenKind::Word) {
        while (isBulkWordChar(source[end])) {
            end += 1;
        }
        word = asWord(source.view(pos(), end));
    }

    tok = {
        .word = word,
        .m_kind = (TokenKind)kind,
        .beginPosition = pos(),
    };

    if (dumpTokens)
        fmt::println("'{:4}': \"{}\"", toShortString((TokenKind)kind), source.view(pos(), end));

    m_position = end;
    skipWhiteSpace();
}

void Parser::reemitLastToken(Token token) {
    m_position = tok.beginPosition;
    tok = token;
}