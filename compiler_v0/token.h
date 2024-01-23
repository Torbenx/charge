#pragma once

#include "log.h"
#include "types.h"
#include <utility>
#include <variant>

#define ENUMERATE_PUNCTUATION_TOKENS \
    PUNC(LeftParen, "(")             \
    PUNC(RightParen, ")")            \
    PUNC(LeftSquare, "[")            \
    PUNC(RightSquare, "]")           \
    PUNC(LeftBrace, "{")             \
    PUNC(RightBrace, "}")            \
                                     \
    /* begin unary */                \
    PUNC(Question, "?")              \
    PUNC(Exclaim, "!")               \
    PUNC(Tilde, "~")                 \
    PUNC(PlusPlus, "++")             \
    PUNC(MinusMinus, "--")           \
    /* begin binary */               \
    PUNC(Plus, "+")                  \
    PUNC(Minus, "-")                 \
    PUNC(Star, "*")                  \
    /* end unary */                  \
    PUNC(Amp, "&")                   \
    PUNC(Hat, "^")                   \
    PUNC(Vert, "|")                  \
    PUNC(Slash, "/")                 \
    PUNC(Percent, "%")               \
    PUNC(LessLess, "<<")             \
    PUNC(GreaterGreater, ">>")       \
    PUNC(AmpAmp, "&&")               \
    PUNC(VertVert, "||")             \
    PUNC(ExclaimEqual, "!=")         \
    PUNC(EqualEqual, "==")           \
    PUNC(Less, "<")                  \
    PUNC(LessEqual, "<=")            \
    PUNC(Greater, ">")               \
    PUNC(GreaterEqual, ">=")         \
    /* end binary */                 \
    /* begin update */               \
    PUNC(Equal, "=")                 \
    PUNC(PlusEqual, "+=")            \
    PUNC(MinusEqual, "-=")           \
    PUNC(StarEqual, "*=")            \
    PUNC(AmpEqual, "&=")             \
    PUNC(HatEqual, "^=")             \
    PUNC(VertEqual, "|=")            \
    PUNC(SlashEqual, "/=")           \
    PUNC(PercentEqual, "%=")         \
    PUNC(LessLessEqual, "<<=")       \
    PUNC(GreaterGreaterEqual, ">>=") \
    PUNC(AmpAmpEqual, "&&=")         \
    PUNC(VertVertEqual, "||=")       \
    /* end update */                 \
                                     \
    PUNC(Comma, ",")                 \
    PUNC(Point, ".")                 \
    PUNC(Colon, ":")                 \
    PUNC(ColonColon, "::")           \
    PUNC(SemiColon, ";")             \
    PUNC(FatArrow, "=>")             \
    PUNC(DoubleArrow, "<=>")         \
    PUNC(Arrow, "->")

#define ENUMERTATE_TOKENS             \
    TOKEN(Word) /* abc123 */          \
    TOKEN(NumericLiteral) /* 123 */   \
    TOKEN(CharacterLiteral) /* 'a' */ \
                                      \
    TOKEN(EOS)                        \
                                      \
    TOKEN(LineComment) /* // */       \
    TOKEN(BlockComment) /* */         \
    TOKEN(Newline)

enum class Token : uint32_t {
    Invalid,
#define PUNC(t, spelling) t,
#define TOKEN(t) t,
    ENUMERTATE_TOKENS
        ENUMERATE_PUNCTUATION_TOKENS
#undef TOKEN
#undef PUNC

            COUNT,

    FirstBracket = LeftParen,
    LastBracket = RightBrace,
    FirstUnaryOp = Question,
    FirstBinaryOp = Plus,
    LastUnaryOp = Star,
    LastBinaryOp = GreaterEqual,
    FirstUpdateOp = Equal,
    LastUpdateOp = VertVertEqual,
    FirstComment = LineComment,
    LastComment = BlockComment,
    FirstWhitespace = FirstComment,
    LastWhitespace = Newline,
};
inline bool isUnaryOp(Token kind) {
    return kind >= Token::FirstUnaryOp && kind <= Token::LastUnaryOp;
}
inline bool isBinaryOp(Token kind) {
    return kind >= Token::FirstBinaryOp && kind <= Token::LastBinaryOp;
}
inline bool isUpdateOp(Token kind) {
    return kind >= Token::FirstUpdateOp && kind <= Token::LastUpdateOp;
}
inline bool isBracket(Token kind) {
    return kind >= Token::FirstBracket && kind <= Token::LastBracket;
}
inline bool isLeftBracket(Token kind) {
    return isBracket(kind) && (std::to_underlying(kind) & 1) == (std::to_underlying(Token::LeftParen) & 1);
}
inline bool isRightBracket(Token kind) {
    return isBracket(kind) && (std::to_underlying(kind) & 1) == (std::to_underlying(Token::RightParen) & 1);
}
inline Token leftToRightBracket(Token kind) {
    VERIFY(isLeftBracket(kind));
    return (Token)(std::to_underlying(kind) + 1);
}
inline Token rightToLeftBracket(Token kind) {
    VERIFY(isRightBracket(kind));
    return (Token)(std::to_underlying(kind) - 1);
}
inline bool isComment(Token kind) {
    return kind >= Token::FirstComment && kind <= Token::LastComment;
}
inline bool isWhitespaceToken(Token kind) {
    return kind >= Token::FirstWhitespace && kind <= Token::LastWhitespace;
}
std::string_view toSmallString(Token);
std::string_view nameString(Token);

struct NumericLiteral {
};
struct CharacterLiteral {
    char character = '\0';
};

using TokenData = std::variant<std::monostate, Word, NumericLiteral, CharacterLiteral>;
struct TokenWithData {
    Token tok = Token::Invalid;
    TokenData tokData = {};
    bool valid() const;
};

struct SingleTokenSourceRange;
struct LocalSourceLocation {
private:
    constexpr LocalSourceLocation(uint32_t tokenStreamOffset, bool atEnd)
        : isAtEnd(atEnd), tokenStreamOffset(tokenStreamOffset) { }
    friend struct SingleTokenSourceRange;

public:
    uint32_t isAtEnd : 8;
    uint32_t tokenStreamOffset : 24;

    constexpr bool operator==(const LocalSourceLocation& other) const {
        return isAtEnd == other.isAtEnd && tokenStreamOffset == other.tokenStreamOffset;
    }
};
class LocalSourceRange {
private:
    LocalSourceLocation m_first;
    LocalSourceLocation m_last;

public:
    constexpr LocalSourceRange(LocalSourceLocation first, LocalSourceLocation last)
        : m_first(first), m_last(last) {
        VERIFY((int_t)last.tokenStreamOffset >= (int_t)first.tokenStreamOffset);
    }
    constexpr LocalSourceLocation first() const { return m_first; }
    constexpr LocalSourceLocation last() const { return m_last; }
};
struct SingleTokenSourceRange {
    uint32_t _unused : 8 = 0;
    uint32_t tokenStreamOffset : 24 = 0;

    constexpr SingleTokenSourceRange() = default;
    constexpr explicit SingleTokenSourceRange(uint32_t tokenStreamOffset)
        : tokenStreamOffset(tokenStreamOffset) { }

    constexpr LocalSourceLocation first() const { return { tokenStreamOffset, false }; }
    constexpr LocalSourceLocation last() const { return { tokenStreamOffset, true }; }
    constexpr operator LocalSourceRange() const { return { first(), last() }; }
};
struct WordAndLocation {
    Word word;
    SingleTokenSourceRange location;

    constexpr operator Word() const { return word; }
};