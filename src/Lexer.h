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

const char* toShortString(TokenKind);
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
    return kind != TokenKind::Invalid && kind < TokenKind::COUNT && kind != TokenKind::DummyOp1 && kind != TokenKind::DummyOp2;
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

    constexpr bool isOperator() { return ::isOperator(kind()); }
    constexpr bool isUnaryOp() { return ::isUnaryOp(kind()); }
    constexpr bool isBinaryOp() { return ::isBinaryOp(kind()); }
    constexpr bool isGoodToken() { return ::isGoodToken(kind()); }
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