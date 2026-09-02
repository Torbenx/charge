#pragma once

#include <EnumTable.h>
#include <parse/IdentifierTable.h>

namespace parse {

inline constexpr ConstWordStringTable words {
    wordWithId("assert", KEYWORD_WORD_ID),
    wordWithId("break", KEYWORD_WORD_ID),
    wordWithId("catch", KEYWORD_WORD_ID),
    wordWithId("const", KEYWORD_WORD_ID),
    wordWithId("continue", KEYWORD_WORD_ID),
    wordWithId("destroy", KEYWORD_WORD_ID),
    wordWithId("discard", KEYWORD_WORD_ID),
    wordWithId("do", KEYWORD_WORD_ID),
    wordWithId("elif", KEYWORD_WORD_ID),
    wordWithId("else", KEYWORD_WORD_ID),
    wordWithId("for", KEYWORD_WORD_ID),
    wordWithId("if", KEYWORD_WORD_ID),
    wordWithId("impl", KEYWORD_WORD_ID),
    wordWithId("let", KEYWORD_WORD_ID),
    wordWithId("return", KEYWORD_WORD_ID),
    wordWithId("shared", KEYWORD_WORD_ID),
    wordWithId("static", KEYWORD_WORD_ID),
    wordWithId("try", KEYWORD_WORD_ID),
    wordWithId("unique", KEYWORD_WORD_ID),
    wordWithId("var", KEYWORD_WORD_ID),
    wordWithId("while", KEYWORD_WORD_ID),
    wordWithId("base", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("context", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("enum", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("fn", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("incomplete", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("namespace", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("open", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("struct", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("template", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("trait", SPECIAL_IDENTIFIER_WORD_ID),
    wordWithId("virtual", SPECIAL_IDENTIFIER_WORD_ID),
    wordInIdRange("bool", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("const_shared_ref", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("const_unique_ref", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("copy", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("error", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("expression_category", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("false", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("from", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("function_id", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("function_signature", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("logical_not", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("member_ptr", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("member_type", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("return_type", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("parent_type", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("pointee_type", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("ptr", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("self_type", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("self", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("shared_ref", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("sig", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("T", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("template_id", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("template_signature", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("true", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("type", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("unique_ref", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("value", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
    wordInIdRange("(generated_identifier)", FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1),
};
inline constexpr Word generated_identifier = words["(generated_identifier)"];

enum class LexerToken : uint8_t {
    LeftParen, // (
    RightParen, // )
    LeftSquare, // [
    RightSquare, // ]
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
    CharacterLiteral,
    NumericLiteral,
    Identifier,
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
    Base, // base
    Context, // context
    Enum, // enum
    Fn, // fn
    Incomplete, // incomplete
    Namespace, // namespace
    Open, // open
    Struct, // struct
    Template, // template
    Trait, // trait
    Virtual, // virtual
    EOS,
    COUNT,

    FirstPunctuation = LeftParen,
    LastPunctuation = MinusGreater,

    FirstKeyword = Assert,
    LastKeyword = While,

    FirstSpecialIdentifier = Base,
    LastSpecialIdentifier = Virtual,

    Invalid = 255
};
std::string_view nameString(LexerToken);
constexpr bool isKeyword(LexerToken token) {
    return LexerToken::FirstKeyword <= token && token <= LexerToken::LastKeyword;
}
constexpr bool isSpecialIdentifier(LexerToken token) {
    return LexerToken::FirstSpecialIdentifier <= token && token <= LexerToken::LastSpecialIdentifier;
}

inline constexpr EnumTable<LexerToken, std::string_view> fixedSpelling = {
    "",
    {
        { LexerToken::LeftParen, "(" },
        { LexerToken::RightParen, ")" },
        { LexerToken::LeftSquare, "[" },
        { LexerToken::RightSquare, "]" },
        { LexerToken::LeftBrace, "{" },
        { LexerToken::RightBrace, "}" },
        { LexerToken::Exclaim, "!" },
        { LexerToken::Tilde, "~" },
        { LexerToken::PlusPlus, "++" },
        { LexerToken::MinusMinus, "--" },
        { LexerToken::Plus, "+" },
        { LexerToken::Minus, "-" },
        { LexerToken::Star, "*" },
        { LexerToken::Amp, "&" },
        { LexerToken::Hat, "^" },
        { LexerToken::Vert, "|" },
        { LexerToken::Slash, "/" },
        { LexerToken::Percent, "%" },
        { LexerToken::LessLess, "<<" },
        { LexerToken::GreaterGreater, ">>" },
        { LexerToken::AmpAmp, "&&" },
        { LexerToken::VertVert, "||" },
        { LexerToken::ExclaimEqual, "!=" },
        { LexerToken::EqualEqual, "==" },
        { LexerToken::Less, "<" },
        { LexerToken::LessEqual, "<=" },
        { LexerToken::Greater, ">" },
        { LexerToken::GreaterEqual, ">=" },
        { LexerToken::Equal, "=" },
        { LexerToken::PlusEqual, "+=" },
        { LexerToken::MinusEqual, "-=" },
        { LexerToken::StarEqual, "*=" },
        { LexerToken::AmpEqual, "&=" },
        { LexerToken::HatEqual, "^=" },
        { LexerToken::VertEqual, "|=" },
        { LexerToken::SlashEqual, "/=" },
        { LexerToken::PercentEqual, "%=" },
        { LexerToken::LessLessEqual, "<<=" },
        { LexerToken::GreaterGreaterEqual, ">>=" },
        { LexerToken::AmpAmpEqual, "&&=" },
        { LexerToken::VertVertEqual, "||=" },
        { LexerToken::Comma, "," },
        { LexerToken::Point, "." },
        { LexerToken::Colon, ":" },
        { LexerToken::ColonColon, "::" },
        { LexerToken::SemiColon, ";" },
        { LexerToken::EqualGreater, "=>" },
        { LexerToken::LessEqualGreater, "<=>" },
        { LexerToken::MinusGreater, "->" },
        { LexerToken::Assert, "assert" },
        { LexerToken::Break, "break" },
        { LexerToken::Catch, "catch" },
        { LexerToken::Const, "const" },
        { LexerToken::Continue, "continue" },
        { LexerToken::Destroy, "destroy" },
        { LexerToken::Discard, "discard" },
        { LexerToken::Do, "do" },
        { LexerToken::Elif, "elif" },
        { LexerToken::Else, "else" },
        { LexerToken::For, "for" },
        { LexerToken::If, "if" },
        { LexerToken::Impl, "impl" },
        { LexerToken::Let, "let" },
        { LexerToken::Return, "return" },
        { LexerToken::Shared, "shared" },
        { LexerToken::Static, "static" },
        { LexerToken::Try, "try" },
        { LexerToken::Unique, "unique" },
        { LexerToken::Var, "var" },
        { LexerToken::While, "while" },
        { LexerToken::Base, "base" },
        { LexerToken::Context, "context" },
        { LexerToken::Enum, "enum" },
        { LexerToken::Fn, "fn" },
        { LexerToken::Incomplete, "incomplete" },
        { LexerToken::Namespace, "namespace" },
        { LexerToken::Open, "open" },
        { LexerToken::Struct, "struct" },
        { LexerToken::Template, "template" },
        { LexerToken::Trait, "trait" },
        { LexerToken::Virtual, "virtual" },
    },
};

enum class State : uint8_t {
    Start,
    Expression,
    AfterExpression,
    CommaAfterExpressionInArguments,
    CommaAfterExpressionInParameters,
    CommaElse,
    Argument,
    CheckDesignatedArgument,
    MaybeDesignatedArgument,
    FirstArgumentParen,
    FirstArgumentSquare,
    FirstArgumentBrace,
    MemberAccess,
    StaticAccess,
    AfterStatement,
    Statement,
    LetStatement,
    VarStatement,
    AfterReturn,
    CheckElseBranch,
    ElseBranch,
    AfterSimpleVariableDeclarationId,
    AfterVariableDeclarationId,
    VariableType,
    ReferenceModifier,
    AfterReferenceModifier,
    AfterVariableModifier,
    AfterVariableUniqueModifier,
    AfterVariableSharedModifier,
    AfterVariableConstModifier,
    ReturnType,
    AfterReturnTypeModifier,
    Borrow,
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

std::span<const State> thenStates(State);
std::span<const LexerToken> possibleTokens(State);

}
