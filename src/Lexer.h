#pragma once

#include "log.h"
#include <cstdint>
#include <string_view>
#include <utility>

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
    ExclaimEqual, // !=
    EqualEqual, // ==
    Amp, // &
    AmpAmp, // &&
    Hat, // ^
    Vert, // |
    VertVert, // ||
    Star, // *
    Slash, // /
    Percent, // %
    Less, // <
    LessLess, // <<
    LessEqual, // <=
    Greater, // >
    GreaterGreater, // >>
    GreaterEqual, // >=
    // end binary
    // begin assign
    Equal, // =
    PlusEqual, // +=
    MinusEqual, // -=
    AmpEqual, // &=
    HatEqual, // ^=
    VertEqual, // |=
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
    FirstAssignOp = Equal,
    LastAssignOp = GreaterGreaterEqual,

    Comma, // ,
    Point, // .
    Colon, // :
    ColonColon, // ::
    SemiColon, // ;
    Word, // abc123
    COUNT,
};

const char* toShortString(TokenKind);
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
constexpr bool isGoodToken(TokenKind kind) {
    return kind != TokenKind::Invalid && kind < TokenKind::COUNT;
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

enum TokenFlags : uint32_t {
    HasLeadingWhiteSpace = 0x01,
    HasTrailingWhiteSpace = 0x02,
};

struct Token {
    static constexpr int KIND_BITS = 8;
    static_assert((1 << KIND_BITS) > (int)TokenKind::COUNT);

    uint32_t start = 0;
    uint32_t length : 8 = 0;
    uint32_t m_kind : KIND_BITS = 0;
    uint32_t flags : 16 = 0;

    TokenKind kind() const { return (TokenKind)m_kind; }
    constexpr bool hasLeadingWhiteSpace() const {
        return flags & HasLeadingWhiteSpace;
    }
    constexpr bool hasTrailingWhiteSpace() const {
        return flags & HasTrailingWhiteSpace;
    }
};

struct SourceBuffer {
    const uint8_t* buffer = nullptr;
    SourceBuffer(const uint8_t* buffer)
        : buffer(buffer) { }
    SourceBuffer(const char* buffer)
        : SourceBuffer((const uint8_t*)buffer) { }

    constexpr const uint8_t& operator[](uint32_t i) const {
        return buffer[i];
    }

    std::string_view sourceCode(Token t) const {
        return { (const char*)(&buffer[t.start]), t.length };
    }
};

struct Lexer {
    SourceBuffer buffer;
    uint32_t m_position = 0;
    uint32_t pos() const { return m_position; }
    bool dumpTokens = false;

    Lexer(SourceBuffer, bool dump = false);

    void advance();
    uint32_t nextNonWhiteSpace(uint32_t pos) const;

    Token tok = {};
};

void dumpTokens(Lexer&);