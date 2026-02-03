#pragma once

#include <WordStringTable.h>

namespace parse {

inline constexpr ConstWordStringTable words {
    wordInIdRange("assert", 0, 1),
    wordInIdRange("break", 0, 1),
    wordInIdRange("catch", 0, 1),
    wordInIdRange("const", 0, 1),
    wordInIdRange("continue", 0, 1),
    wordInIdRange("destroy", 0, 1),
    wordInIdRange("discard", 0, 1),
    wordInIdRange("do", 0, 1),
    wordInIdRange("elif", 0, 1),
    wordInIdRange("else", 0, 1),
    wordInIdRange("for", 0, 1),
    wordInIdRange("if", 0, 1),
    wordInIdRange("impl", 0, 1),
    wordInIdRange("let", 0, 1),
    wordInIdRange("return", 0, 1),
    wordInIdRange("shared", 0, 1),
    wordInIdRange("static", 0, 1),
    wordInIdRange("try", 0, 1),
    wordInIdRange("unique", 0, 1),
    wordInIdRange("var", 0, 1),
    wordInIdRange("while", 0, 1),
    wordInIdRange("enum", 1, 2),
    wordInIdRange("fn", 1, 2),
    wordInIdRange("has", 1, 2),
    wordInIdRange("incomplete", 1, 2),
    wordInIdRange("namespace", 1, 2),
    wordInIdRange("open", 1, 2),
    wordInIdRange("struct", 1, 2),
    wordInIdRange("template", 1, 2),
    wordInIdRange("trait", 1, 2),
    wordInIdRange("virtual", 1, 2),
    wordInIdRange("bool", 2, Word::MAX_ID + 1),
    wordInIdRange("const_shared_ref", 2, Word::MAX_ID + 1),
    wordInIdRange("const_unique_ref", 2, Word::MAX_ID + 1),
    wordInIdRange("copy", 2, Word::MAX_ID + 1),
    wordInIdRange("error", 2, Word::MAX_ID + 1),
    wordInIdRange("expression_category", 2, Word::MAX_ID + 1),
    wordInIdRange("false", 2, Word::MAX_ID + 1),
    wordInIdRange("from", 2, Word::MAX_ID + 1),
    wordInIdRange("function_id", 2, Word::MAX_ID + 1),
    wordInIdRange("function_signature", 2, Word::MAX_ID + 1),
    wordInIdRange("logical_not", 2, Word::MAX_ID + 1),
    wordInIdRange("member_ptr", 2, Word::MAX_ID + 1),
    wordInIdRange("member_type", 2, Word::MAX_ID + 1),
    wordInIdRange("return_type", 2, Word::MAX_ID + 1),
    wordInIdRange("parent_type", 2, Word::MAX_ID + 1),
    wordInIdRange("pointee_type", 2, Word::MAX_ID + 1),
    wordInIdRange("ptr", 2, Word::MAX_ID + 1),
    wordInIdRange("self_type", 2, Word::MAX_ID + 1),
    wordInIdRange("self", 2, Word::MAX_ID + 1),
    wordInIdRange("shared_ref", 2, Word::MAX_ID + 1),
    wordInIdRange("sig", 2, Word::MAX_ID + 1),
    wordInIdRange("T", 2, Word::MAX_ID + 1),
    wordInIdRange("template_id", 2, Word::MAX_ID + 1),
    wordInIdRange("template_signature", 2, Word::MAX_ID + 1),
    wordInIdRange("true", 2, Word::MAX_ID + 1),
    wordInIdRange("type", 2, Word::MAX_ID + 1),
    wordInIdRange("unique_ref", 2, Word::MAX_ID + 1),
    wordInIdRange("value", 2, Word::MAX_ID + 1),
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
    Assert, // assert
    Break, // break
    Catch, // catch
    Const, // const
    Continue, // continue
    Destroy, // destroy
    Discard, // discard
    Do, // do
    Elif, // elif
    Else, // else
    For, // for
    If, // if
    Impl, // impl
    Let, // let
    Return, // return
    Shared, // shared
    Static, // static
    Try, // try
    Unique, // unique
    Var, // var
    While, // while
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
    Argument,
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
    ImplExpression,
    AfterImplExpression,
    ImplAccessExpression,
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
    StructDeclarationId,
    AfterStructDeclarationId,
    StructDeclarationBody,
    MemberDeclaration,
    EnumDeclarationId,
    AfterEnumDeclarationId,
    EnumDeclarationBody,
    EnumValueDeclaration,
    AfterEnumValueDeclarationId,
    AfterStatic,
    StaticVarVariableDeclaration,
    StaticOpenVariableDeclaration,
    AfterDeclaration,
    Error,
};
std::string_view nameString(State);

}
