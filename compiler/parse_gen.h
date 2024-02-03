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
enum class Keyword : uint8_t {
    BEGIN = 49,
    Analysis = 49, // analysis
    Assert = 50, // assert
    Assign = 51, // assign
    Break = 52, // break
    Catch = 53, // catch
    Continue = 54, // continue
    Do = 55, // do
    Elif = 56, // elif
    Else = 57, // else
    False = 58, // false
    Fn = 59, // fn
    For = 60, // for
    Forward = 61, // forward
    Guard = 62, // guard
    If = 63, // if
    In = 64, // in
    Inout = 65, // inout
    Let = 66, // let
    Loop = 67, // loop
    Match = 68, // match
    Namespace = 69, // namespace
    Object = 70, // object
    Out = 71, // out
    Return = 72, // return
    Static = 73, // static
    Struct = 74, // struct
    Template = 75, // template
    Trait = 76, // trait
    True = 77, // true
    Try = 78, // try
    Var = 79, // var
    While = 80, // while
    With = 81, // with
    END = 82,
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
    Analysis = 49, // analysis
    Assert = 50, // assert
    Assign = 51, // assign
    Break = 52, // break
    Catch = 53, // catch
    Continue = 54, // continue
    Do = 55, // do
    Elif = 56, // elif
    Else = 57, // else
    False = 58, // false
    Fn = 59, // fn
    For = 60, // for
    Forward = 61, // forward
    Guard = 62, // guard
    If = 63, // if
    In = 64, // in
    Inout = 65, // inout
    Let = 66, // let
    Loop = 67, // loop
    Match = 68, // match
    Namespace = 69, // namespace
    Object = 70, // object
    Out = 71, // out
    Return = 72, // return
    Static = 73, // static
    Struct = 74, // struct
    Template = 75, // template
    Trait = 76, // trait
    True = 77, // true
    Try = 78, // try
    Var = 79, // var
    While = 80, // while
    With = 81, // with
    Identifier = 82,
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
