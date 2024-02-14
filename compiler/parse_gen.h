#pragma once

#include "types.h"

namespace parse {

enum class Token : uint8_t {
    LeftParen, // (
    RightParen, // )
    LeftSqure, // [
    RightSqure, // ]
    LeftBrace, // {
    RightBrace, // }
    Exclaim, // !
    Tilde, // ~
    PlusPlus, // ++
    MinusMinus, // --
    Plus, // +
    Minus, // -
    Star, // *
    Amp, // &
    Hat, // ^
    Vert, // |
    Slash, // /
    Percent, // %
    LessLess, // <<
    GreaterGreater, // >>
    AmpAmp, // &&
    VertVert, // ||
    ExclaimEqual, // !=
    EqualEqual, // ==
    Less, // <
    LessEqual, // <=
    Greater, // >
    GreaterEqual, // >=
    Equal, // =
    PlusEqual, // +=
    MinusEqual, // -=
    StarEqual, // *=
    AmpEqual, // &=
    HatEqual, // ^=
    VertEqual, // |=
    SlashEqual, // /=
    PercentEqual, // %=
    LessLessEqual, // <<=
    GreaterGreaterEqual, // >>=
    AmpAmpEqual, // &&=
    VertVertEqual, // ||=
    Comma, // ,
    Point, // .
    Colon, // :
    ColonColon, // ::
    SemiColon, // ;
    EqualGreater, // =>
    LessEqualGreater, // <=>
    MinusGreater, // ->
    Identifier,
};
std::string_view nameString(Token);

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
    CheckVarAfterLet,
    LocalDeclaration,
    AfterLocalDeclarationId,
    Error,
};
std::string_view nameString(State);

}
