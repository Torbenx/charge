#include "parse.h"

namespace parse {

std::string_view nameString(Token token) {
    switch (token) {
    case Token::LeftParen:
        return "LeftParen";
    case Token::RightParen:
        return "RightParen";
    case Token::LeftSqure:
        return "LeftSqure";
    case Token::RightSqure:
        return "RightSqure";
    case Token::LeftBrace:
        return "LeftBrace";
    case Token::RightBrace:
        return "RightBrace";
    case Token::Exclaim:
        return "Exclaim";
    case Token::Tilde:
        return "Tilde";
    case Token::PlusPlus:
        return "PlusPlus";
    case Token::MinusMinus:
        return "MinusMinus";
    case Token::Plus:
        return "Plus";
    case Token::Minus:
        return "Minus";
    case Token::Star:
        return "Star";
    case Token::Amp:
        return "Amp";
    case Token::Hat:
        return "Hat";
    case Token::Vert:
        return "Vert";
    case Token::Slash:
        return "Slash";
    case Token::Percent:
        return "Percent";
    case Token::LessLess:
        return "LessLess";
    case Token::GreaterGreater:
        return "GreaterGreater";
    case Token::AmpAmp:
        return "AmpAmp";
    case Token::VertVert:
        return "VertVert";
    case Token::ExclaimEqual:
        return "ExclaimEqual";
    case Token::EqualEqual:
        return "EqualEqual";
    case Token::Less:
        return "Less";
    case Token::LessEqual:
        return "LessEqual";
    case Token::Greater:
        return "Greater";
    case Token::GreaterEqual:
        return "GreaterEqual";
    case Token::Equal:
        return "Equal";
    case Token::PlusEqual:
        return "PlusEqual";
    case Token::MinusEqual:
        return "MinusEqual";
    case Token::StarEqual:
        return "StarEqual";
    case Token::AmpEqual:
        return "AmpEqual";
    case Token::HatEqual:
        return "HatEqual";
    case Token::VertEqual:
        return "VertEqual";
    case Token::SlashEqual:
        return "SlashEqual";
    case Token::PercentEqual:
        return "PercentEqual";
    case Token::LessLessEqual:
        return "LessLessEqual";
    case Token::GreaterGreaterEqual:
        return "GreaterGreaterEqual";
    case Token::AmpAmpEqual:
        return "AmpAmpEqual";
    case Token::VertVertEqual:
        return "VertVertEqual";
    case Token::Comma:
        return "Comma";
    case Token::Point:
        return "Point";
    case Token::Colon:
        return "Colon";
    case Token::ColonColon:
        return "ColonColon";
    case Token::SemiColon:
        return "SemiColon";
    case Token::EqualGreater:
        return "EqualGreater";
    case Token::LessEqualGreater:
        return "LessEqualGreater";
    case Token::MinusGreater:
        return "MinusGreater";
    case Token::Identifier:
        return "Identifier";
    }
}

std::string_view nameString(State state) {
    switch (state) {
    case State::Expression:
        return "Expression";
    case State::AfterExpression:
        return "AfterExpression";
    case State::Statement:
        return "Statement";
    case State::SingleOrCompoundStatement:
        return "SingleOrCompoundStatement";
    case State::CommaAfterExpression:
        return "CommaAfterExpression";
    case State::CommaElse:
        return "CommaElse";
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
    case State::AccessPunctuation:
        return "AccessPunctuation";
    case State::CheckVarAfterLet:
        return "CheckVarAfterLet";
    case State::LocalDeclaration:
        return "LocalDeclaration";
    case State::AfterLocalDeclarationId:
        return "AfterLocalDeclarationId";
    case State::Error:
        return "Error";
    }
}

}
