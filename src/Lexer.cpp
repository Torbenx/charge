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
        TokenKind equal = bare;
        TokenKind repeatEqual = repeat;
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
                table[c - 0x20 + 0x60 * 0] = { input.bare, 1 };
                table[c - 0x20 + 0x60 * 1] = { input.repeat, input.repeat != input.bare ? 2 : 1 };
                table[c - 0x20 + 0x60 * 2] = { input.equal, input.equal != input.bare ? 2 : 1 };
                table[c - 0x20 + 0x60 * 3] = { input.repeatEqual, input.repeatEqual != input.repeat ? 3 : (input.repeatEqual != input.bare ? 2 : 1) };
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

        { '^', { .bare = Hat, .equal = HatEqual } },
        { '!', { .bare = Exclaim, .equal = ExclaimEqual } },
        { '%', { .bare = Percent, .equal = PercentEqual } },
        { '/', { .bare = Slash, .equal = SlashEqual } },
        { '*', { .bare = Star, .equal = StarEqual } },
        { '=', { .bare = Equal, .repeat = EqualEqual } },

        { '+', { .bare = Plus, .repeat = PlusPlus, .equal = PlusEqual } },
        { '-', { .bare = Minus, .repeat = MinusMinus, .equal = MinusEqual } },
        { '&', { .bare = Amp, .repeat = AmpAmp, .equal = AmpEqual, .repeatEqual = AmpAmpEqual } },
        { '|', { .bare = Vert, .repeat = VertVert, .equal = VertEqual, .repeatEqual = VertVertEqual } },

        { '<', { .bare = Less, .repeat = LessLess, .equal = LessEqual, .repeatEqual = LessLessEqual } },
        { '>', { .bare = Greater, .repeat = GreaterGreater, .equal = GreaterEqual, .repeatEqual = GreaterGreaterEqual } },

        { '0', '9', { IntegerLiteral } },

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

void Lexer::reset(SourceBuffer buffer) {
    source = buffer;
    uint32_t firstToken = nextNonWhiteSpace(0);
    m_position = firstToken;
    tok.flags |= firstToken > 0 ? TokenFlags::HasLeadingWhiteSpace : 0;
    advance();
}

uint32_t Lexer::nextNonWhiteSpace(uint32_t pos) const {
    while (isWhiteSpace(source[pos])) {
        pos += 1;
    }
    return pos;
}

void Lexer::advance() {
    u8 head = source[pos()];
    if (head == '\0') [[unlikely]] {
        tok = {
            .start = pos(),
            .length = 0,
            .m_kind = (uint32_t)TokenKind::EOS,
            .flags = 0,
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
    // ->

    uint32_t end = pos() + 1;
    bool repeat = source[end] == head;
    end += repeat ? 1 : 0;
    bool equal = source[end] == '=';
    uint32_t idx = (uint32_t)head - 0x20 + (repeat ? 0x60 : 0) + (equal ? 0x60 * 2 : 0);
    auto [kind, advance] = TableHolder::table.table[idx];

    end = pos() + advance;
    if ((TokenKind)kind == TokenKind::Word) {
        while (isBulkWordChar(source[end])) {
            end += 1;
        }
    } else if ((TokenKind)kind == TokenKind::IntegerLiteral) {
        while (isIntLiteralChar(source[end])) {
            end += 1;
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
        fmt::println("'{:4}': \"{}\"", toShortString(tok.kind()), source.view(tok));
}