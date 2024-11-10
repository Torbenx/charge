#pragma once

#include <WordTable.h>

namespace parse {

inline constexpr ConstWordStringTable words {
    keyword("analysis"),
    keyword("assert"),
    keyword("assign"),
    keyword("break"),
    keyword("catch"),
    keyword("continue"),
    keyword("destroy"),
    keyword("discard"),
    keyword("do"),
    keyword("elif"),
    keyword("else"),
    keyword("fn"),
    keyword("for"),
    keyword("forward"),
    keyword("guard"),
    keyword("has"),
    keyword("if"),
    keyword("in"),
    keyword("incomplete"),
    keyword("inout"),
    keyword("let"),
    keyword("loop"),
    keyword("match"),
    keyword("namespace"),
    keyword("object"),
    keyword("out"),
    keyword("property"),
    keyword("return"),
    keyword("static"),
    keyword("struct"),
    keyword("template"),
    keyword("trait"),
    keyword("try"),
    keyword("var"),
    keyword("virtual"),
    keyword("while"),
    keyword("with"),
    "bool",
    "error",
    "false",
    "function_id",
    "function_signature",
    "member_ptr",
    "member_type",
    "parent_type",
    "pointee_type",
    "ptr",
    "sig",
    "template_id",
    "template_signature",
    "true",
    "type",
};
enum class LexerToken : uint8_t {
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
    Analysis, // analysis
    Assert, // assert
    Assign, // assign
    Break, // break
    Catch, // catch
    Continue, // continue
    Destroy, // destroy
    Discard, // discard
    Do, // do
    Elif, // elif
    Else, // else
    Fn, // fn
    For, // for
    Forward, // forward
    Guard, // guard
    Has, // has
    If, // if
    In, // in
    Incomplete, // incomplete
    Inout, // inout
    Let, // let
    Loop, // loop
    Match, // match
    Namespace, // namespace
    Object, // object
    Out, // out
    Property, // property
    Return, // return
    Static, // static
    Struct, // struct
    Template, // template
    Trait, // trait
    Try, // try
    Var, // var
    Virtual, // virtual
    While, // while
    With, // with
    Identifier,
    Literal,
    EOS
};
std::string_view nameString(LexerToken);

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
    AfterReturn,
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
    TemplatedDeclarationWithAttributes,
    AfterTemplate,
    AfterTemplateParameters,
    FunctionDeclarationId,
    AfterFunctionDeclarationId,
    AfterFunctionParameters,
    TypeDeclarationId,
    AfterTypeDeclarationId,
    TypeDeclarationBody,
    MemberDeclaration,
    AfterStatic,
    StaticLetVariableDeclaration,
    StaticVarVariableDeclaration,
    AfterDeclaration,
    Error,
};
std::string_view nameString(State);

}
