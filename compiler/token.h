#pragma once

#include "log.h"
#include "types.h"
#include <utility>
#include <variant>

#define ENUMERTATE_TOKENS                \
    TOKEN(LeftParen) /* ( */             \
    TOKEN(RightParen) /* ) */            \
    TOKEN(LeftAngle) /* [ */             \
    TOKEN(RightAngle) /* ] */            \
    TOKEN(LeftBrace) /* { */             \
    TOKEN(RightBrace) /* } */            \
                                         \
    /* begin unary */                    \
    TOKEN(Question) /* ? */              \
    TOKEN(Exclaim) /* ! */               \
    TOKEN(Tilde) /* ~ */                 \
    TOKEN(PlusPlus) /* ++ */             \
    TOKEN(MinusMinus) /* -- */           \
    /* begin binary */                   \
    TOKEN(Plus) /* + */                  \
    TOKEN(Minus) /* - */                 \
    TOKEN(Star) /* * */                  \
    /* end unary */                      \
    TOKEN(Amp) /* & */                   \
    TOKEN(Hat) /* ^ */                   \
    TOKEN(Vert) /* | */                  \
    TOKEN(Slash) /* / */                 \
    TOKEN(Percent) /* % */               \
    TOKEN(LessLess) /* << */             \
    TOKEN(GreaterGreater) /* >> */       \
    TOKEN(ExclaimEqual) /* != */         \
    TOKEN(EqualEqual) /* == */           \
    TOKEN(Less) /* < */                  \
    TOKEN(LessEqual) /* <= */            \
    TOKEN(Greater) /* > */               \
    TOKEN(GreaterEqual) /* >= */         \
    TOKEN(AmpAmp) /* && */               \
    TOKEN(VertVert) /* || */             \
    /* end binary */                     \
    /* begin update */                   \
    TOKEN(Equal) /* = */                 \
    TOKEN(PlusEqual) /* += */            \
    TOKEN(MinusEqual) /* -= */           \
    TOKEN(StarEqual) /* *= */            \
    TOKEN(AmpEqual) /* &= */             \
    TOKEN(HatEqual) /* ^= */             \
    TOKEN(VertEqual) /* |= */            \
    TOKEN(SlashEqual) /* /= */           \
    TOKEN(PercentEqual) /* %= */         \
    TOKEN(LessLessEqual) /* <<= */       \
    TOKEN(GreaterGreaterEqual) /* >>= */ \
    TOKEN(AmpAmpEqual) /* &&= */         \
    TOKEN(VertVertEqual) /* ||= */       \
    /* end update */                     \
                                         \
    TOKEN(Comma) /* , */                 \
    TOKEN(Point) /* . */                 \
    TOKEN(Colon) /* : */                 \
    TOKEN(ColonColon) /* :: */           \
    TOKEN(SemiColon) /* ; */             \
    TOKEN(FatArrow) /* => */             \
    TOKEN(DoubleArrow) /* <=> */         \
    TOKEN(Arrow) /* -> */                \
    TOKEN(Word) /* abc123 */             \
    TOKEN(NumericLiteral) /* 123 */      \
    TOKEN(CharacterLiteral) /* 'a' */    \
                                         \
    TOKEN(EOS)                           \
                                         \
    TOKEN(LineComment) /* // */          \
    TOKEN(BlockComment) /* /> ... </ */  \
    TOKEN(Newline)

enum class Token : uint32_t {
    Invalid,

#define TOKEN(t) t,
    ENUMERTATE_TOKENS
#undef TOKEN

        COUNT,

    FirstBracket = LeftParen,
    LastBracket = RightBrace,
    FirstUnaryOp = Question,
    FirstBinaryOp = Plus,
    LastUnaryOp = Star,
    LastBinaryOp = VertVert,
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

using TokenData = std::variant<std::nullopt_t, Word, NumericLiteral, CharacterLiteral>;
struct TokenWithData {
    Token tok = Token::Invalid;
    TokenData tokData = std::nullopt;
    bool valid() const;
};

struct SingleTokenSourceRange;
struct LocalSourceLocation {
private:
    LocalSourceLocation(uint32_t tokenStreamOffset, bool atEnd)
        : isAtEnd(atEnd), tokenStreamOffset(tokenStreamOffset) { }
    friend struct SingleTokenSourceRange;

public:
    uint32_t isAtEnd : 8;
    uint32_t tokenStreamOffset : 24;

    bool operator==(const LocalSourceLocation& other) const {
        return isAtEnd == other.isAtEnd && tokenStreamOffset == other.tokenStreamOffset;
    }
};
class LocalSourceRange {
private:
    LocalSourceLocation m_first;
    LocalSourceLocation m_last;

public:
    LocalSourceRange(LocalSourceLocation first, LocalSourceLocation last)
        : m_first(first), m_last(last) {
        VERIFY((int_t)last.tokenStreamOffset >= (int_t)first.tokenStreamOffset);
    }
    LocalSourceLocation first() const { return m_first; }
    LocalSourceLocation last() const { return m_last; }
};
struct SingleTokenSourceRange {
    uint32_t _unused : 8 = 0;
    uint32_t tokenStreamOffset : 24 = 0;

    SingleTokenSourceRange() = default;
    explicit SingleTokenSourceRange(uint32_t tokenStreamOffset)
        : tokenStreamOffset(tokenStreamOffset) { }

    LocalSourceLocation first() const { return { tokenStreamOffset, false }; }
    LocalSourceLocation last() const { return { tokenStreamOffset, true }; }
    operator LocalSourceRange() const { return { first(), last() }; }
};
struct WordAndLocation {
    Word word;
    SingleTokenSourceRange location;

    operator Word() const { return word; }
};