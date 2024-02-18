#pragma once

#include "WordTable.h"

namespace parse {

inline constexpr ConstWordStringTable words {
    keyword("if"),
    keyword("elif"),
    keyword("else"),
    keyword("match"),
    keyword("for"),
    keyword("while"),
    keyword("do"),
    keyword("return"),
    keyword("break"),
    keyword("continue"),
    keyword("loop"),
    keyword("guard"),
    keyword("try"),
    keyword("catch"),
    keyword("with"),
    keyword("analysis"),
    keyword("assert"),
    keyword("namespace"),
    keyword("struct"),
    keyword("trait"),
    keyword("object"),
    keyword("fn"),
    keyword("static"),
    keyword("template"),
    keyword("var"),
    keyword("let"),
    keyword("in"),
    keyword("inout"),
    keyword("out"),
    keyword("forward"),
    keyword("assign"),
};
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
    If, // if
    Elif, // elif
    Else, // else
    Match, // match
    For, // for
    While, // while
    Do, // do
    Return, // return
    Break, // break
    Continue, // continue
    Loop, // loop
    Guard, // guard
    Try, // try
    Catch, // catch
    With, // with
    Analysis, // analysis
    Assert, // assert
    Namespace, // namespace
    Struct, // struct
    Trait, // trait
    Object, // object
    Fn, // fn
    Static, // static
    Template, // template
    Var, // var
    Let, // let
    In, // in
    Inout, // inout
    Out, // out
    Forward, // forward
    Assign, // assign
    Identifier,
};
std::string_view nameString(Token);

enum class State {
    Expression,
    AfterExpression,
    CommaAfterExpression,
    CommaElse,
    CheckDesignatedArgument,
    MaybeDesignatedArgument,
    FirstArgumentParen,
    FirstArgumentSquare,
    FirstArgumentBrace,
    AccessPunctuation,
    SingleOrCompoundStatement,
    AfterStatement,
    Statement,
    ElseBranch,
    CheckVarAfterLet,
    VariableDeclaration,
    AfterVariableDeclarationId,
    AfterParameters,
    FirstParameter,
    Parameter,
    NoDeclaration,
    NamespaceDeclaration,
    NamespaceDeclarationId,
    AfterNamespaceDeclarationId,
    NamespaceDeclarationBody,
    TemplatedDeclaration,
    FunctionDeclarationId,
    AfterFunctionDeclarationId,
    AfterFunctionParameters,
    AfterDeclaration,
    Error,
};
std::string_view nameString(State);

}
