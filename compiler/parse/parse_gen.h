#pragma once

#include <WordTable.h>

namespace parse {

inline constexpr ConstWordStringTable words {
    keyword("analysis"),
    keyword("assert"),
    keyword("assign"),
    keyword("break"),
    keyword("catch"),
    keyword("const"),
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
    keyword("impls"),
    keyword("incomplete"),
    keyword("let"),
    keyword("loop"),
    keyword("match"),
    keyword("namespace"),
    keyword("object"),
    keyword("property"),
    keyword("return"),
    keyword("shared"),
    keyword("static"),
    keyword("struct"),
    keyword("template"),
    keyword("trait"),
    keyword("try"),
    keyword("unique"),
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
    Const, // const
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
    Impls, // impls
    Incomplete, // incomplete
    Let, // let
    Loop, // loop
    Match, // match
    Namespace, // namespace
    Object, // object
    Property, // property
    Return, // return
    Shared, // shared
    Static, // static
    Struct, // struct
    Template, // template
    Trait, // trait
    Try, // try
    Unique, // unique
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
    LetStatement,
    VarStatement,
    AfterReturn,
    ElseBranch,
    AfterSimpleVariableDeclarationId,
    AfterVariableDeclarationId,
    VariableType,
    AfterVariableModifier,
    AfterVariableUniqueModifier,
    AfterVariableSharedModifier,
    AfterVariableConstModifier,
    AfterParameters,
    FirstParameter,
    Parameter,
    VarParameter,
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
    StaticVarVariableDeclaration,
    AfterDeclaration,
    Error,
};
std::string_view nameString(State);

}
