#pragma once

#include "types.h"

namespace parse {
enum class Punctuation : uint8_t {
    BEGIN = 0,
    LeftParen = 0, // (
    RightParen = 1, // )
    LeftSqure = 2, // [
    RightSqure = 3, // ]
    LeftBrace = 4, // {
    RightBrace = 5, // }
    Exclaim = 6, // !
    Tilde = 7, // ~
    PlusPlus = 8, // ++
    MinusMinus = 9, // --
    Plus = 10, // +
    Minus = 11, // -
    Star = 12, // *
    Amp = 13, // &
    Hat = 14, // ^
    Vert = 15, // |
    Slash = 16, // /
    Percent = 17, // %
    LessLess = 18, // <<
    GreaterGreater = 19, // >>
    AmpAmp = 20, // &&
    VertVert = 21, // ||
    ExclaimEqual = 22, // !=
    EqualEqual = 23, // ==
    Less = 24, // <
    LessEqual = 25, // <=
    Greater = 26, // >
    GreaterEqual = 27, // >=
    Equal = 28, // =
    PlusEqual = 29, // +=
    MinusEqual = 30, // -=
    StarEqual = 31, // *=
    AmpEqual = 32, // &=
    HatEqual = 33, // ^=
    VertEqual = 34, // |=
    SlashEqual = 35, // /=
    PercentEqual = 36, // %=
    LessLessEqual = 37, // <<=
    GreaterGreaterEqual = 38, // >>=
    AmpAmpEqual = 39, // &&=
    VertVertEqual = 40, // ||=
    Comma = 41, // ,
    Point = 42, // .
    Colon = 43, // :
    ColonColon = 44, // ::
    SemiColon = 45, // ;
    EqualGreater = 46, // =>
    LessEqualGreater = 47, // <=>
    MinusGreater = 48, // ->
    END = 49,
};
enum class Token : uint8_t {
    LeftParen = 0, // (
    RightParen = 1, // )
    LeftSqure = 2, // [
    RightSqure = 3, // ]
    LeftBrace = 4, // {
    RightBrace = 5, // }
    Exclaim = 6, // !
    Tilde = 7, // ~
    PlusPlus = 8, // ++
    MinusMinus = 9, // --
    Plus = 10, // +
    Minus = 11, // -
    Star = 12, // *
    Amp = 13, // &
    Hat = 14, // ^
    Vert = 15, // |
    Slash = 16, // /
    Percent = 17, // %
    LessLess = 18, // <<
    GreaterGreater = 19, // >>
    AmpAmp = 20, // &&
    VertVert = 21, // ||
    ExclaimEqual = 22, // !=
    EqualEqual = 23, // ==
    Less = 24, // <
    LessEqual = 25, // <=
    Greater = 26, // >
    GreaterEqual = 27, // >=
    Equal = 28, // =
    PlusEqual = 29, // +=
    MinusEqual = 30, // -=
    StarEqual = 31, // *=
    AmpEqual = 32, // &=
    HatEqual = 33, // ^=
    VertEqual = 34, // |=
    SlashEqual = 35, // /=
    PercentEqual = 36, // %=
    LessLessEqual = 37, // <<=
    GreaterGreaterEqual = 38, // >>=
    AmpAmpEqual = 39, // &&=
    VertVertEqual = 40, // ||=
    Comma = 41, // ,
    Point = 42, // .
    Colon = 43, // :
    ColonColon = 44, // ::
    SemiColon = 45, // ;
    EqualGreater = 46, // =>
    LessEqualGreater = 47, // <=>
    MinusGreater = 48, // ->
    Identifier = 49,
};
enum class State {
    Expression,
    AfterExpression,
    Statement,
    SingleOrCompoundStatement,
    CommaAfterExpression,
    CommaElse,
    CheckDesignatedArgument,
    MaybeDesignatedArgument,
    FirstArgumentParen,
    FirstArgumentSquare,
    FirstArgumentBrace,
    AccessPunctuation,
    Error,
};

}
