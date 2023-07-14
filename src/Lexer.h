#pragma once

#include "log.h"
#include <cstdint>
#include <string_view>
#include <utility>

struct Word {
    uint32_t id = 0;
    explicit operator bool() const { return id != 0; }
    bool operator==(const Word& other) const { return id == other.id; }
};

enum class TokenKind : uint32_t {
    Invalid,

    LeftParen, // (
    RightParen, // )
    LeftAngle, // [
    RightAngle, // ]
    LeftBrace, // {
    RightBrace, // }

    FirstBracket = LeftParen,
    LastBracket = RightBrace,

    // begin unary
    Exclaim, // !
    Tilde, // ~
    PlusPlus, // ++
    MinusMinus, // --
    // begin binary
    Plus, // +
    Minus, // -
    // end unary
    Amp, // &
    AmpAmp, // &&
    Hat, // ^
    Vert, // |
    VertVert, // ||
    Star, // *
    Slash, // /
    Percent, // %
    LessLess, // <<
    GreaterGreater, // >>
    ExclaimEqual, // !=
    EqualEqual, // ==
    Less, // <
    LessEqual, // <=
    Greater, // >
    GreaterEqual, // >=
    // end binary
    // begin assign
    PlusEqual, // +=
    MinusEqual, // -=
    AmpEqual, // &=
    AmpAmpEqual, // &&=
    HatEqual, // ^=
    VertEqual, // |=
    VertVertEqual, // ||=
    StarEqual, // *=
    SlashEqual, // /=
    PercentEqual, // %=
    LessLessEqual, // <<=
    GreaterGreaterEqual, // >>=
    // end assign

    FirstUnaryOp = Exclaim,
    FirstBinaryOp = Plus,
    LastUnaryOp = Minus,
    LastBinaryOp = GreaterEqual,
    FirstAssignOp = PlusEqual,
    LastAssignOp = GreaterGreaterEqual,

    Equal, // =
    Question, // ?
    Comma, // ,
    Point, // .
    Colon, // :
    ColonColon, // ::
    SemiColon, // ;
    FatArrow, // =>
    Word, // abc123

    IntegerLiteral,
    FirstLiteral = IntegerLiteral,
    LastLiteral = IntegerLiteral,

    EOS,

    // =>
    // ->
    // <=>
    // '
    // "
    // ..

    COUNT,
};

const char* toShortString(TokenKind);
const char* exampleString(TokenKind);
template<>
struct fmt::formatter<TokenKind> : formatter<string_view> {
    template<typename FormatContext>
    auto format(TokenKind kind, FormatContext& ctx) const {
        return formatter<string_view>::format(toShortString(kind), ctx);
    }
};

constexpr bool isUnaryOp(TokenKind kind) {
    return kind >= TokenKind::FirstUnaryOp && kind <= TokenKind::LastUnaryOp;
}
constexpr bool isBinaryOp(TokenKind kind) {
    return kind >= TokenKind::FirstBinaryOp && kind <= TokenKind::LastBinaryOp;
}
constexpr bool isAssignOp(TokenKind kind) {
    return kind >= TokenKind::FirstAssignOp && kind <= TokenKind::LastAssignOp;
}
constexpr bool isBracket(TokenKind kind) {
    return kind >= TokenKind::FirstBracket && kind <= TokenKind::LastBracket;
}
constexpr bool isLeftBracket(TokenKind kind) {
    return isBracket(kind) && (std::to_underlying(kind) & 1) == (std::to_underlying(TokenKind::LeftParen) & 1);
}
constexpr bool isRightBracket(TokenKind kind) {
    return isBracket(kind) && (std::to_underlying(kind) & 1) == (std::to_underlying(TokenKind::RightParen) & 1);
}
constexpr TokenKind leftToRightBracket(TokenKind kind) {
    VERIFY(isLeftBracket(kind));
    return (TokenKind)(std::to_underlying(kind) + 1);
}
constexpr TokenKind rightToLeftBracket(TokenKind kind) {
    VERIFY(isRightBracket(kind));
    return (TokenKind)(std::to_underlying(kind) - 1);
}
constexpr bool isLiteral(TokenKind kind) {
    return kind >= TokenKind::FirstLiteral && kind <= TokenKind::LastLiteral;
}

struct Token {
    Word word = {};
    TokenKind m_kind = TokenKind::Invalid;
    uint32_t beginPosition = 0;

    TokenKind kind() const { return m_kind; }
};

struct SourceBuffer {
    const uint8_t* buffer = nullptr;
    SourceBuffer() = default;
    SourceBuffer(const uint8_t* buffer)
        : buffer(buffer) { }
    SourceBuffer(const char* buffer)
        : SourceBuffer((const uint8_t*)buffer) { }

    constexpr const uint8_t& operator[](uint32_t i) const {
        return buffer[i];
    }

    std::string_view view(uint32_t pos, uint32_t end) {
        return { (const char*)&buffer[pos], end - pos };
    }
};