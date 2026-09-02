#include <parse/parse_gen.h>

#include <parse/TokenBuffer.h>

namespace parse {

std::string_view nameString(LexerToken token) {
    switch (token) {
    case LexerToken::LeftParen:
        return "LeftParen";
    case LexerToken::RightParen:
        return "RightParen";
    case LexerToken::LeftSquare:
        return "LeftSquare";
    case LexerToken::RightSquare:
        return "RightSquare";
    case LexerToken::LeftBrace:
        return "LeftBrace";
    case LexerToken::RightBrace:
        return "RightBrace";
    case LexerToken::Exclaim:
        return "Exclaim";
    case LexerToken::Tilde:
        return "Tilde";
    case LexerToken::PlusPlus:
        return "PlusPlus";
    case LexerToken::MinusMinus:
        return "MinusMinus";
    case LexerToken::Plus:
        return "Plus";
    case LexerToken::Minus:
        return "Minus";
    case LexerToken::Star:
        return "Star";
    case LexerToken::Amp:
        return "Amp";
    case LexerToken::Hat:
        return "Hat";
    case LexerToken::Vert:
        return "Vert";
    case LexerToken::Slash:
        return "Slash";
    case LexerToken::Percent:
        return "Percent";
    case LexerToken::LessLess:
        return "LessLess";
    case LexerToken::GreaterGreater:
        return "GreaterGreater";
    case LexerToken::AmpAmp:
        return "AmpAmp";
    case LexerToken::VertVert:
        return "VertVert";
    case LexerToken::ExclaimEqual:
        return "ExclaimEqual";
    case LexerToken::EqualEqual:
        return "EqualEqual";
    case LexerToken::Less:
        return "Less";
    case LexerToken::LessEqual:
        return "LessEqual";
    case LexerToken::Greater:
        return "Greater";
    case LexerToken::GreaterEqual:
        return "GreaterEqual";
    case LexerToken::Equal:
        return "Equal";
    case LexerToken::PlusEqual:
        return "PlusEqual";
    case LexerToken::MinusEqual:
        return "MinusEqual";
    case LexerToken::StarEqual:
        return "StarEqual";
    case LexerToken::AmpEqual:
        return "AmpEqual";
    case LexerToken::HatEqual:
        return "HatEqual";
    case LexerToken::VertEqual:
        return "VertEqual";
    case LexerToken::SlashEqual:
        return "SlashEqual";
    case LexerToken::PercentEqual:
        return "PercentEqual";
    case LexerToken::LessLessEqual:
        return "LessLessEqual";
    case LexerToken::GreaterGreaterEqual:
        return "GreaterGreaterEqual";
    case LexerToken::AmpAmpEqual:
        return "AmpAmpEqual";
    case LexerToken::VertVertEqual:
        return "VertVertEqual";
    case LexerToken::Comma:
        return "Comma";
    case LexerToken::Point:
        return "Point";
    case LexerToken::Colon:
        return "Colon";
    case LexerToken::ColonColon:
        return "ColonColon";
    case LexerToken::SemiColon:
        return "SemiColon";
    case LexerToken::EqualGreater:
        return "EqualGreater";
    case LexerToken::LessEqualGreater:
        return "LessEqualGreater";
    case LexerToken::MinusGreater:
        return "MinusGreater";
    case LexerToken::Assert:
        return "Assert";
    case LexerToken::Break:
        return "Break";
    case LexerToken::Catch:
        return "Catch";
    case LexerToken::Const:
        return "Const";
    case LexerToken::Continue:
        return "Continue";
    case LexerToken::Destroy:
        return "Destroy";
    case LexerToken::Discard:
        return "Discard";
    case LexerToken::Do:
        return "Do";
    case LexerToken::Elif:
        return "Elif";
    case LexerToken::Else:
        return "Else";
    case LexerToken::For:
        return "For";
    case LexerToken::If:
        return "If";
    case LexerToken::Impl:
        return "Impl";
    case LexerToken::Let:
        return "Let";
    case LexerToken::Return:
        return "Return";
    case LexerToken::Shared:
        return "Shared";
    case LexerToken::Static:
        return "Static";
    case LexerToken::Try:
        return "Try";
    case LexerToken::Unique:
        return "Unique";
    case LexerToken::Var:
        return "Var";
    case LexerToken::While:
        return "While";
    case LexerToken::Base:
        return "Base";
    case LexerToken::Context:
        return "Context";
    case LexerToken::Enum:
        return "Enum";
    case LexerToken::Fn:
        return "Fn";
    case LexerToken::Incomplete:
        return "Incomplete";
    case LexerToken::Namespace:
        return "Namespace";
    case LexerToken::Open:
        return "Open";
    case LexerToken::Struct:
        return "Struct";
    case LexerToken::Template:
        return "Template";
    case LexerToken::Trait:
        return "Trait";
    case LexerToken::Virtual:
        return "Virtual";
    case LexerToken::Identifier:
        return "Identifier";
    case LexerToken::CharacterLiteral:
        return "CharacterLiteral";
    case LexerToken::NumericLiteral:
        return "NumericLiteral";
    case LexerToken::EOS:
        return "EOS";
    case LexerToken::Invalid:
        return "Invalid";
    default:
        VERIFY_NOT_REACHED();
    }
}

std::string_view nameString(State state) {
    switch (state) {
    case State::Start:
        return "Start";
    case State::Expression:
        return "Expression";
    case State::AfterExpression:
        return "AfterExpression";
    case State::CommaAfterExpressionInArguments:
        return "CommaAfterExpressionInArguments";
    case State::CommaAfterExpressionInParameters:
        return "CommaAfterExpressionInParameters";
    case State::CommaElse:
        return "CommaElse";
    case State::Argument:
        return "Argument";
    case State::CheckDesignatedArgument:
        return "CheckDesignatedArgument";
    case State::MaybeDesignatedArgument:
        return "MaybeDesignatedArgument";
    case State::FirstArgumentParen:
        return "FirstArgumentParen";
    case State::FirstArgumentSquare:
        return "FirstArgumentSquare";
    case State::FirstArgumentBrace:
        return "FirstArgumentBrace";
    case State::MemberAccess:
        return "MemberAccess";
    case State::StaticAccess:
        return "StaticAccess";
    case State::AfterStatement:
        return "AfterStatement";
    case State::Statement:
        return "Statement";
    case State::LetStatement:
        return "LetStatement";
    case State::VarStatement:
        return "VarStatement";
    case State::AfterReturn:
        return "AfterReturn";
    case State::CheckElseBranch:
        return "CheckElseBranch";
    case State::ElseBranch:
        return "ElseBranch";
    case State::AfterSimpleVariableDeclarationId:
        return "AfterSimpleVariableDeclarationId";
    case State::AfterVariableDeclarationId:
        return "AfterVariableDeclarationId";
    case State::VariableType:
        return "VariableType";
    case State::ReferenceModifier:
        return "ReferenceModifier";
    case State::AfterReferenceModifier:
        return "AfterReferenceModifier";
    case State::AfterVariableModifier:
        return "AfterVariableModifier";
    case State::AfterVariableUniqueModifier:
        return "AfterVariableUniqueModifier";
    case State::AfterVariableSharedModifier:
        return "AfterVariableSharedModifier";
    case State::AfterVariableConstModifier:
        return "AfterVariableConstModifier";
    case State::ReturnType:
        return "ReturnType";
    case State::AfterReturnTypeModifier:
        return "AfterReturnTypeModifier";
    case State::Borrow:
        return "Borrow";
    case State::AfterParameters:
        return "AfterParameters";
    case State::FirstParameter:
        return "FirstParameter";
    case State::Parameter:
        return "Parameter";
    case State::VarParameter:
        return "VarParameter";
    case State::ImplExpression:
        return "ImplExpression";
    case State::AfterImplExpression:
        return "AfterImplExpression";
    case State::ImplAccessExpression:
        return "ImplAccessExpression";
    case State::NoDeclaration:
        return "NoDeclaration";
    case State::NamespaceDeclaration:
        return "NamespaceDeclaration";
    case State::NamespaceDeclarationId:
        return "NamespaceDeclarationId";
    case State::AfterNamespaceDeclarationId:
        return "AfterNamespaceDeclarationId";
    case State::NamespaceDeclarationBody:
        return "NamespaceDeclarationBody";
    case State::TemplatedDeclaration:
        return "TemplatedDeclaration";
    case State::TemplatedDeclarationWithAttributes:
        return "TemplatedDeclarationWithAttributes";
    case State::AfterTemplate:
        return "AfterTemplate";
    case State::AfterTemplateParameters:
        return "AfterTemplateParameters";
    case State::FunctionDeclarationId:
        return "FunctionDeclarationId";
    case State::AfterFunctionDeclarationId:
        return "AfterFunctionDeclarationId";
    case State::AfterFunctionParameters:
        return "AfterFunctionParameters";
    case State::StructDeclarationId:
        return "StructDeclarationId";
    case State::AfterStructDeclarationId:
        return "AfterStructDeclarationId";
    case State::StructDeclarationBody:
        return "StructDeclarationBody";
    case State::MemberDeclaration:
        return "MemberDeclaration";
    case State::EnumDeclarationId:
        return "EnumDeclarationId";
    case State::AfterEnumDeclarationId:
        return "AfterEnumDeclarationId";
    case State::EnumDeclarationBody:
        return "EnumDeclarationBody";
    case State::EnumValueDeclaration:
        return "EnumValueDeclaration";
    case State::AfterEnumValueDeclarationId:
        return "AfterEnumValueDeclarationId";
    case State::AfterStatic:
        return "AfterStatic";
    case State::StaticVarVariableDeclaration:
        return "StaticVarVariableDeclaration";
    case State::StaticOpenVariableDeclaration:
        return "StaticOpenVariableDeclaration";
    case State::AfterDeclaration:
        return "AfterDeclaration";
    case State::Error:
        return "Error";
    default:
        VERIFY_NOT_REACHED();
    }
}

std::span<const LexerToken> possibleTokens(State state) {
    switch (state) {
    case State::Start: {
        return {};
    }
    case State::Expression: {
        static constexpr std::array r = { LexerToken::Exclaim, LexerToken::Tilde, LexerToken::Plus, LexerToken::Minus, LexerToken::PlusPlus, LexerToken::MinusMinus, LexerToken::Star, LexerToken::Point, LexerToken::LeftParen, LexerToken::If, LexerToken::Identifier, LexerToken::CharacterLiteral, LexerToken::NumericLiteral };
        return r;
    }
    case State::AfterExpression: {
        static constexpr std::array r = { LexerToken::PlusPlus, LexerToken::MinusMinus, LexerToken::Plus, LexerToken::Minus, LexerToken::Star, LexerToken::Amp, LexerToken::Hat, LexerToken::Vert, LexerToken::Slash, LexerToken::Percent, LexerToken::LessLess, LexerToken::GreaterGreater, LexerToken::AmpAmp, LexerToken::VertVert, LexerToken::ExclaimEqual, LexerToken::EqualEqual, LexerToken::Less, LexerToken::LessEqual, LexerToken::Greater, LexerToken::GreaterEqual, LexerToken::PlusEqual, LexerToken::MinusEqual, LexerToken::StarEqual, LexerToken::AmpEqual, LexerToken::HatEqual, LexerToken::VertEqual, LexerToken::SlashEqual, LexerToken::PercentEqual, LexerToken::LessLessEqual, LexerToken::GreaterGreaterEqual, LexerToken::AmpAmpEqual, LexerToken::VertVertEqual, LexerToken::Point, LexerToken::ColonColon, LexerToken::LeftParen, LexerToken::LeftSquare, LexerToken::LeftBrace, LexerToken::RightParen, LexerToken::RightSquare, LexerToken::RightBrace, LexerToken::Comma, LexerToken::SemiColon, LexerToken::EqualGreater, LexerToken::Colon, LexerToken::Equal };
        return r;
    }
    case State::CommaAfterExpressionInArguments: {
        static constexpr std::array r = { LexerToken::Else, LexerToken::RightParen, LexerToken::RightSquare, LexerToken::RightBrace };
        return r;
    }
    case State::CommaAfterExpressionInParameters: {
        static constexpr std::array r = { LexerToken::RightParen };
        return r;
    }
    case State::CommaElse: {
        static constexpr std::array r = { LexerToken::EqualGreater };
        return r;
    }
    case State::Argument: {
        return {};
    }
    case State::CheckDesignatedArgument: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::MaybeDesignatedArgument: {
        static constexpr std::array r = { LexerToken::Colon };
        return r;
    }
    case State::FirstArgumentParen: {
        static constexpr std::array r = { LexerToken::RightParen };
        return r;
    }
    case State::FirstArgumentSquare: {
        static constexpr std::array r = { LexerToken::RightSquare };
        return r;
    }
    case State::FirstArgumentBrace: {
        static constexpr std::array r = { LexerToken::RightBrace };
        return r;
    }
    case State::MemberAccess: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::StaticAccess: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::AfterStatement: {
        return {};
    }
    case State::Statement: {
        static constexpr std::array r = { LexerToken::LeftBrace, LexerToken::If, LexerToken::Let, LexerToken::Var, LexerToken::Return, LexerToken::Destroy, LexerToken::Discard, LexerToken::RightBrace };
        return r;
    }
    case State::LetStatement: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::VarStatement: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::AfterReturn: {
        static constexpr std::array r = { LexerToken::SemiColon };
        return r;
    }
    case State::CheckElseBranch: {
        static constexpr std::array r = { LexerToken::Else };
        return r;
    }
    case State::ElseBranch: {
        static constexpr std::array r = { LexerToken::Colon };
        return r;
    }
    case State::AfterSimpleVariableDeclarationId: {
        static constexpr std::array r = { LexerToken::Colon };
        return r;
    }
    case State::AfterVariableDeclarationId: {
        static constexpr std::array r = { LexerToken::Colon, LexerToken::Equal, LexerToken::SemiColon, LexerToken::Comma, LexerToken::RightParen };
        return r;
    }
    case State::VariableType: {
        static constexpr std::array r = { LexerToken::Amp };
        return r;
    }
    case State::ReferenceModifier: {
        static constexpr std::array r = { LexerToken::Unique, LexerToken::Shared, LexerToken::Const, LexerToken::LeftParen };
        return r;
    }
    case State::AfterReferenceModifier: {
        return {};
    }
    case State::AfterVariableModifier: {
        static constexpr std::array r = { LexerToken::Equal, LexerToken::SemiColon, LexerToken::Comma, LexerToken::RightParen };
        return r;
    }
    case State::AfterVariableUniqueModifier: {
        static constexpr std::array r = { LexerToken::Const };
        return r;
    }
    case State::AfterVariableSharedModifier: {
        static constexpr std::array r = { LexerToken::Const };
        return r;
    }
    case State::AfterVariableConstModifier: {
        static constexpr std::array r = { LexerToken::Shared, LexerToken::Unique };
        return r;
    }
    case State::ReturnType: {
        static constexpr std::array r = { LexerToken::Amp };
        return r;
    }
    case State::AfterReturnTypeModifier: {
        static constexpr std::array r = { LexerToken::LeftSquare };
        return r;
    }
    case State::Borrow: {
        static constexpr std::array r = { LexerToken::Unique };
        return r;
    }
    case State::AfterParameters: {
        return {};
    }
    case State::FirstParameter: {
        static constexpr std::array r = { LexerToken::RightParen };
        return r;
    }
    case State::Parameter: {
        static constexpr std::array r = { LexerToken::Identifier, LexerToken::Var };
        return r;
    }
    case State::VarParameter: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::ImplExpression: {
        static constexpr std::array r = { LexerToken::Identifier, LexerToken::LeftParen };
        return r;
    }
    case State::AfterImplExpression: {
        static constexpr std::array r = { LexerToken::ColonColon, LexerToken::LeftBrace };
        return r;
    }
    case State::ImplAccessExpression: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::NoDeclaration: {
        static constexpr std::array r = { LexerToken::RightBrace };
        return r;
    }
    case State::NamespaceDeclaration: {
        static constexpr std::array r = { LexerToken::Namespace };
        return r;
    }
    case State::NamespaceDeclarationId: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::AfterNamespaceDeclarationId: {
        static constexpr std::array r = { LexerToken::Colon };
        return r;
    }
    case State::NamespaceDeclarationBody: {
        static constexpr std::array r = { LexerToken::LeftBrace };
        return r;
    }
    case State::TemplatedDeclaration: {
        static constexpr std::array r = { LexerToken::Template, LexerToken::Incomplete, LexerToken::Virtual, LexerToken::Fn, LexerToken::Struct, LexerToken::Enum, LexerToken::Static };
        return r;
    }
    case State::TemplatedDeclarationWithAttributes: {
        static constexpr std::array r = { LexerToken::Template, LexerToken::Incomplete, LexerToken::Virtual, LexerToken::Fn, LexerToken::Struct, LexerToken::Enum, LexerToken::Static };
        return r;
    }
    case State::AfterTemplate: {
        static constexpr std::array r = { LexerToken::LeftParen };
        return r;
    }
    case State::AfterTemplateParameters: {
        return {};
    }
    case State::FunctionDeclarationId: {
        static constexpr std::array r = { LexerToken::Identifier, LexerToken::Impl };
        return r;
    }
    case State::AfterFunctionDeclarationId: {
        static constexpr std::array r = { LexerToken::LeftParen };
        return r;
    }
    case State::AfterFunctionParameters: {
        static constexpr std::array r = { LexerToken::Colon, LexerToken::MinusGreater, LexerToken::EqualGreater };
        return r;
    }
    case State::StructDeclarationId: {
        static constexpr std::array r = { LexerToken::Identifier, LexerToken::Impl };
        return r;
    }
    case State::AfterStructDeclarationId: {
        static constexpr std::array r = { LexerToken::Colon };
        return r;
    }
    case State::StructDeclarationBody: {
        static constexpr std::array r = { LexerToken::LeftBrace };
        return r;
    }
    case State::MemberDeclaration: {
        static constexpr std::array r = { LexerToken::Base, LexerToken::Identifier };
        return r;
    }
    case State::EnumDeclarationId: {
        static constexpr std::array r = { LexerToken::Identifier, LexerToken::Impl };
        return r;
    }
    case State::AfterEnumDeclarationId: {
        static constexpr std::array r = { LexerToken::Colon };
        return r;
    }
    case State::EnumDeclarationBody: {
        static constexpr std::array r = { LexerToken::LeftBrace };
        return r;
    }
    case State::EnumValueDeclaration: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::AfterEnumValueDeclarationId: {
        static constexpr std::array r = { LexerToken::Equal, LexerToken::SemiColon };
        return r;
    }
    case State::AfterStatic: {
        static constexpr std::array r = { LexerToken::Var, LexerToken::Open, LexerToken::Impl, LexerToken::Identifier };
        return r;
    }
    case State::StaticVarVariableDeclaration: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::StaticOpenVariableDeclaration: {
        static constexpr std::array r = { LexerToken::Identifier };
        return r;
    }
    case State::AfterDeclaration: {
        return {};
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

std::span<const State> thenStates(State state) {
    switch (state) {
    case State::Start: {
        static constexpr std::array r = { State::Error, State::NamespaceDeclaration, State::NoDeclaration, State::TemplatedDeclaration };
        return r;
    }
    case State::Expression: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterExpression: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::CommaAfterExpressionInArguments: {
        static constexpr std::array r = { State::Argument, State::CheckDesignatedArgument, State::Error, State::Expression };
        return r;
    }
    case State::CommaAfterExpressionInParameters: {
        static constexpr std::array r = { State::Error, State::Parameter };
        return r;
    }
    case State::CommaElse: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::Argument: {
        static constexpr std::array r = { State::CheckDesignatedArgument, State::Error, State::Expression };
        return r;
    }
    case State::CheckDesignatedArgument: {
        static constexpr std::array r = { State::Error, State::Expression };
        return r;
    }
    case State::MaybeDesignatedArgument: {
        static constexpr std::array r = { State::AfterExpression, State::Error };
        return r;
    }
    case State::FirstArgumentParen: {
        static constexpr std::array r = { State::Argument, State::CheckDesignatedArgument, State::Error, State::Expression };
        return r;
    }
    case State::FirstArgumentSquare: {
        static constexpr std::array r = { State::Argument, State::CheckDesignatedArgument, State::Error, State::Expression };
        return r;
    }
    case State::FirstArgumentBrace: {
        static constexpr std::array r = { State::Argument, State::CheckDesignatedArgument, State::Error, State::Expression };
        return r;
    }
    case State::MemberAccess: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::StaticAccess: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterStatement: {
        static constexpr std::array r = { State::AfterDeclaration, State::CheckElseBranch, State::EnumValueDeclaration, State::Error, State::Expression, State::MemberDeclaration, State::NamespaceDeclaration, State::NoDeclaration, State::Statement, State::TemplatedDeclaration };
        return r;
    }
    case State::Statement: {
        static constexpr std::array r = { State::Error, State::Expression };
        return r;
    }
    case State::LetStatement: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::VarStatement: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterReturn: {
        static constexpr std::array r = { State::Error, State::Expression };
        return r;
    }
    case State::CheckElseBranch: {
        static constexpr std::array r = { State::Error, State::Expression, State::Statement };
        return r;
    }
    case State::ElseBranch: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterSimpleVariableDeclarationId: {
        static constexpr std::array r = { State::AfterVariableDeclarationId, State::Error };
        return r;
    }
    case State::AfterVariableDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::VariableType: {
        static constexpr std::array r = { State::Error, State::Expression };
        return r;
    }
    case State::ReferenceModifier: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterReferenceModifier: {
        static constexpr std::array r = { State::AfterReturnTypeModifier, State::AfterVariableModifier, State::Error, State::Expression };
        return r;
    }
    case State::AfterVariableModifier: {
        static constexpr std::array r = { State::Error, State::Expression };
        return r;
    }
    case State::AfterVariableUniqueModifier: {
        static constexpr std::array r = { State::AfterReferenceModifier, State::AfterReturnTypeModifier, State::AfterVariableModifier, State::Error, State::Expression };
        return r;
    }
    case State::AfterVariableSharedModifier: {
        static constexpr std::array r = { State::AfterReferenceModifier, State::AfterReturnTypeModifier, State::AfterVariableModifier, State::Error, State::Expression };
        return r;
    }
    case State::AfterVariableConstModifier: {
        static constexpr std::array r = { State::AfterReferenceModifier, State::AfterReturnTypeModifier, State::AfterVariableModifier, State::Error, State::Expression };
        return r;
    }
    case State::ReturnType: {
        static constexpr std::array r = { State::Error, State::Expression };
        return r;
    }
    case State::AfterReturnTypeModifier: {
        static constexpr std::array r = { State::Error, State::Expression };
        return r;
    }
    case State::Borrow: {
        static constexpr std::array r = { State::Error, State::Expression };
        return r;
    }
    case State::AfterParameters: {
        static constexpr std::array r = { State::AfterFunctionParameters, State::AfterTemplateParameters, State::Error, State::TemplatedDeclarationWithAttributes };
        return r;
    }
    case State::FirstParameter: {
        static constexpr std::array r = { State::Error, State::Parameter };
        return r;
    }
    case State::Parameter: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::VarParameter: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::ImplExpression: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterImplExpression: {
        static constexpr std::array r = { State::AfterEnumDeclarationId, State::AfterFunctionDeclarationId, State::AfterSimpleVariableDeclarationId, State::AfterStructDeclarationId, State::AfterVariableDeclarationId, State::Error };
        return r;
    }
    case State::ImplAccessExpression: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::NoDeclaration: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::NamespaceDeclaration: {
        static constexpr std::array r = { State::Error, State::NoDeclaration, State::TemplatedDeclaration };
        return r;
    }
    case State::NamespaceDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterNamespaceDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::NamespaceDeclarationBody: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::TemplatedDeclaration: {
        static constexpr std::array r = { State::Error, State::NoDeclaration };
        return r;
    }
    case State::TemplatedDeclarationWithAttributes: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterTemplate: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterTemplateParameters: {
        static constexpr std::array r = { State::Error, State::TemplatedDeclarationWithAttributes };
        return r;
    }
    case State::FunctionDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterFunctionDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterFunctionParameters: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::StructDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterStructDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::StructDeclarationBody: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::MemberDeclaration: {
        static constexpr std::array r = { State::Error, State::NoDeclaration, State::TemplatedDeclaration };
        return r;
    }
    case State::EnumDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterEnumDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::EnumDeclarationBody: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::EnumValueDeclaration: {
        static constexpr std::array r = { State::Error, State::NoDeclaration, State::TemplatedDeclaration };
        return r;
    }
    case State::AfterEnumValueDeclarationId: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterStatic: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::StaticVarVariableDeclaration: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::StaticOpenVariableDeclaration: {
        static constexpr std::array r = { State::Error };
        return r;
    }
    case State::AfterDeclaration: {
        static constexpr std::array r = { State::EnumValueDeclaration, State::Error, State::MemberDeclaration, State::NamespaceDeclaration, State::NoDeclaration, State::TemplatedDeclaration };
        return r;
    }
    case State::Error: {
        return {};
    }
    default:
        VERIFY_NOT_REACHED();
    }
}

}
