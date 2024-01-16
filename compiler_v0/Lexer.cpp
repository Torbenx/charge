#include "Parser.h"
#include <array>
#include <ranges>

std::string_view toSmallString(Token token) {
    switch (token) {
    case Token::Invalid:
        return "Invalid";
    default:
        VERIFY_NOT_REACHED();

#define PUNC(t, sp) \
    case Token::t:  \
        return sp;
#define TOKEN(t)   \
    case Token::t: \
        return #t;

        ENUMERTATE_TOKENS
        ENUMERATE_PUNCTUATION_TOKENS

#undef TOKEN
#undef PUNC
    }
    // clang-format on
}
std::string_view nameString(Token token) {
    switch (token) {
    case Token::Invalid:
        return "Invalid";
    default:
        VERIFY_NOT_REACHED();

#define PUNC(t, sp) \
    case Token::t:  \
        return #t;
#define TOKEN(t)   \
    case Token::t: \
        return #t;

        ENUMERTATE_TOKENS
        ENUMERATE_PUNCTUATION_TOKENS

#undef TOKEN
#undef PUNC
    }
}

// advances offset to the next non-whitespace character
static void skipTabsAndSpaces(std::string_view sourceBuffer, int_t& sourceOffset) {
    while (sourceBuffer[sourceOffset] == ' ' || sourceBuffer[sourceOffset] == '\t')
        sourceOffset += 1;
}

static bool isEndOfLineCharacter(uint8_t c) {
    return c == '\n' || c == '\r';
}
// advances offset to the next new line character
static void skipToEndOfLine(std::string_view sourceBuffer, int_t& sourceOffset) {
    while (!isEndOfLineCharacter(sourceBuffer[sourceOffset]))
        sourceOffset += 1;
}

// advances offset to the next '</'
static void skipToEndOfBlockComment(std::string_view sourceBuffer, int_t& sourceOffset) {
    while (sourceBuffer[sourceOffset] != '\0'
        && !(sourceBuffer[sourceOffset] == '<' && sourceBuffer[sourceOffset + 1] == '/')) {
        sourceOffset += 1;
    }
}

static void skipToEndOfCharacterLiteral(std::string_view sourceBuffer, int_t& sourceOffset) {
    for (;;) {
        auto c = sourceBuffer[sourceOffset];
        if (c == '\'' || c == '\n' || c == '\r' || c == '\0')
            break;
        sourceOffset += 1;
    }
}

static bool isBulkWordCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

bool TokenWithData::valid() const {
    if (tok <= Token::Invalid || tok >= Token::COUNT)
        return false;
    if (tok == Token::Word)
        return std::holds_alternative<Word>(tokData);
    if (tok == Token::NumericLiteral)
        return std::holds_alternative<NumericLiteral>(tokData);
    if (tok == Token::CharacterLiteral)
        return std::holds_alternative<CharacterLiteral>(tokData);
    return std::holds_alternative<std::monostate>(tokData);
}
bool Lexer::valid() const {
    if (sourceBuffer.empty())
        return false;
    if (*sourceBuffer.end() != '\0' && sourceBuffer.back() != '\0')
        return false;
    if (tokBegin > sourceOffset)
        return false;
    return TokenWithData::valid();
}

std::string_view Lexer::source(int_t begin, int_t end) const {
    return { sourceBuffer.begin() + begin, sourceBuffer.begin() + end };
}
std::string_view Lexer::tokCommentSource() const {
    auto token = tokenStream.back();
    VERIFY(isComment(token.token()));
    if (token.token() == Token::BlockComment)
        return source(token.begin() + 2, token.end() - 2);
    if (token.token() == Token::LineComment)
        return source(token.begin() + 2, token.end());
    VERIFY_NOT_REACHED();
}

SingleTokenSourceRange Lexer::tokRange() const {
    if (cachedNextToken.tok == Token::Invalid)
        return SingleTokenSourceRange(tokenStream.size() - 1);
    int_t index = tokenStream.size() - 2;
    while (isWhitespaceToken(tokenStream[index].token())) {
        index -= 1;
        VERIFY(index >= 0);
    }
    return SingleTokenSourceRange(index);
}

static std::pair<const StreamToken*, uint32_t> findInStream(const Lexer* lex, LocalSourceLocation loc) {
    auto iter = lex->tokenStream.data() + loc.tokenStreamOffset;
    auto offset = loc.isAtEnd ? iter->end() : iter->begin();
    return { iter, offset };
};

SourcePosition Lexer::sourcePosition(LocalSourceLocation loc) const {
    auto [iter, offset] = findInStream(this, loc);
    while (iter->token() != Token::Newline)
        --iter;
    return { .line = iter->lineNumber(), .column = offset - iter->begin() + 1 };
}

void Lexer::formatLine(std::ostream& out, LocalSourceRange highlight) const {
    auto [beginIter, highlightBegin] = findInStream(this, highlight.first());
    while (beginIter->token() != Token::Newline)
        --beginIter;
    auto [endIter, highlightEnd] = findInStream(this, highlight.last());
    do {
        ++endIter;
    } while (endIter->token() != Token::Newline && endIter < tokenStream.end());
    --endIter;
    uint32_t beginOffset = (beginIter + 1)->begin();
    uint32_t endOffset = endIter->end();
    out << "    " << sourceBuffer.substr(beginOffset, endOffset - beginOffset) << '\n';
    std::string highlightStr;
    highlightStr.resize(highlightBegin - beginOffset, ' ');
    highlightStr.resize(highlightEnd - beginOffset, '^');
    out << "    " << highlightStr << '\n';
}

void Lexer::setSource(std::string_view source) {
    // reset all fields
    (LexerState&)* this = { source };

    if (source.empty())
        return;
    VERIFY(source[source.length()] == '\0');
    VERIFY(source[source.length() - 1] != '\0');
}

void Lexer::nextToken() {

#define HANDLE_LEXER_ACTION(a)                                         \
    {                                                                  \
        ErrorHandler::LexerAction action = a;                          \
        if (action == ErrorHandler::LexerAction::Retry) {              \
            continue;                                                  \
        } else if (action == ErrorHandler::LexerAction::AcceptState) { \
            break;                                                     \
        } else {                                                       \
            VERIFY_NOT_REACHED();                                      \
        }                                                              \
    }

    if (cachedNextToken.tok != Token::Invalid) {
        (TokenWithData&)* this = cachedNextToken;
        cachedNextToken = {};
        return;
    }

    for (;;) {
        skipTabsAndSpaces(sourceBuffer, sourceOffset);

        tokBegin = sourceOffset;
        tok = Token::Invalid;
        tokData = {};

        char c0 = sourceBuffer[sourceOffset];
        switch (c0) {
        case '\0': {
            if (sourceOffset == (int_t)sourceBuffer.length()) {
                tok = Token::EOS;
                break;
            }
            HANDLE_LEXER_ACTION(errorHandler->invalidCharacter(this, '\0'));
        }
        case '\n': {
            tok = Token::Newline;
            lineNumber += 1;
            sourceOffset += 1;
            tokenStream.emit(StreamToken::makeNewline(lineNumber, sourceOffset));
            continue;
        }
        case '\r': {
            tok = Token::Newline;
            lineNumber += 1;
            if (sourceBuffer[sourceOffset + 1] == '\n')
                sourceOffset += 2;
            else
                sourceOffset += 1;
            tokenStream.emit(StreamToken::makeNewline(lineNumber, sourceOffset));
            continue;
        }
        case '\'': {
            tok = Token::CharacterLiteral;
            sourceOffset += 1;
            skipToEndOfCharacterLiteral(sourceBuffer, sourceOffset);
            if (sourceBuffer[sourceOffset] != '\'') [[unlikely]] {
                HANDLE_LEXER_ACTION(errorHandler->unterminatedCharacterLiteral(this, tokBegin, sourceOffset));
            }
            int_t literalBegin = tokBegin + 1;
            int_t literalLength = sourceOffset - literalBegin;
            sourceOffset += 1;
            if (literalLength != 1 || (uint8_t)sourceBuffer[literalBegin] >= 0x80) [[unlikely]] {
                HANDLE_LEXER_ACTION(errorHandler->invalidCharacterLiteral(this, literalBegin, literalBegin + literalLength));
            }
            tokData = CharacterLiteral { sourceBuffer[literalBegin] };
            break;
        }
        case '/': {
            char c1 = sourceBuffer[sourceOffset + 1];
            if (c1 == '/') {
                tok = Token::LineComment;
                skipToEndOfLine(sourceBuffer, sourceOffset);
                tokenStream.emit(StreamToken::make(Token::LineComment, tokBegin, sourceOffset));
                if (instrumenter)
                    instrumenter->handleComment(this);
                continue;
            } else if (c1 == '>') {
                tok = Token::BlockComment;
                skipToEndOfBlockComment(sourceBuffer, sourceOffset);
                if (sourceBuffer[sourceOffset] == '\0') [[unlikely]] {
                    HANDLE_LEXER_ACTION(errorHandler->unterminatedBlockComment(this, tokBegin));
                }
                sourceOffset += 2;
                tokenStream.emit(StreamToken::make(Token::BlockComment, tokBegin, sourceOffset));
                if (instrumenter)
                    instrumenter->handleComment(this);
                continue;
            } else if (c1 == '=') {
                tok = Token::SlashEqual;
                sourceOffset += 2;
            } else {
                tok = Token::Slash;
                sourceOffset += 1;
            }
            break;
        }
        case '<': {
            char c1 = sourceBuffer[sourceOffset + 1];
            char c2 = sourceBuffer[sourceOffset + 2];
            if (c1 == '=') {
                if (c2 == '>') {
                    tok = Token::DoubleArrow;
                    sourceOffset += 3;
                } else {
                    tok = Token::LessEqual;
                    sourceOffset += 2;
                }
            } else if (c1 == '<') {
                if (c2 == '=') {
                    tok = Token::LessLessEqual;
                    sourceOffset += 3;
                } else {
                    tok = Token::LessLess;
                    sourceOffset += 2;
                }
            } else {
                tok = Token::Less;
                sourceOffset += 1;
            }
            break;
        }
        // clang-format off
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
        case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M':
        case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z': 
        case '_': case '$': case '#': {
            tok = Token::Word;
            uint32_t hash = 0;
            do {
                hash = Word::iterateHash(hash, sourceBuffer[sourceOffset]);
                sourceOffset += 1;
            } while (isBulkWordCharacter(sourceBuffer[sourceOffset]));
            hash = Word::finalizeHash(hash);
            tokData = wordTable.getWithHash(source(tokBegin, sourceOffset), hash);
            break;
        }
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': {
            tok = Token::NumericLiteral;
            // TODO: implement parsing num literals
            for (;;) {
                char c = sourceBuffer[sourceOffset];
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')
                    sourceOffset += 1;
                else
                    break;
            }
            tokData = NumericLiteral();
            break;
        }
        // always one character punctuations
        case '(': { tok = Token::LeftParen;   sourceOffset += 1; break; }
        case ')': { tok = Token::RightParen;  sourceOffset += 1; break; }
        case '[': { tok = Token::LeftSquare;  sourceOffset += 1; break; }
        case ']': { tok = Token::RightSquare; sourceOffset += 1; break; }
        case '{': { tok = Token::LeftBrace;   sourceOffset += 1; break; }
        case '}': { tok = Token::RightBrace;  sourceOffset += 1; break; }
        case '?': { tok = Token::Question;    sourceOffset += 1; break; }
        case ',': { tok = Token::Comma;       sourceOffset += 1; break; }
        case '.': { tok = Token::Point;       sourceOffset += 1; break; }
        case '~': { tok = Token::Tilde;       sourceOffset += 1; break; }
        case ';': { tok = Token::SemiColon;   sourceOffset += 1; break; }
        // only followed by equal
        case '!': { if (sourceBuffer[sourceOffset + 1] == '=') { tok = Token::ExclaimEqual; sourceOffset += 2; } else { tok = Token::Exclaim; sourceOffset += 1; } break; }
        case '*': { if (sourceBuffer[sourceOffset + 1] == '=') { tok = Token::StarEqual;    sourceOffset += 2; } else { tok = Token::Star;    sourceOffset += 1; } break; }
        case '^': { if (sourceBuffer[sourceOffset + 1] == '=') { tok = Token::HatEqual;     sourceOffset += 2; } else { tok = Token::Hat;     sourceOffset += 1; } break; }
        case '%': { if (sourceBuffer[sourceOffset + 1] == '=') { tok = Token::PercentEqual; sourceOffset += 2; } else { tok = Token::Percent; sourceOffset += 1; } break; }
        // & | >
        case '&': { char c1 = sourceBuffer[sourceOffset + 1]; char c2 = sourceBuffer[sourceOffset + 2]; 
                    if (c1 == '&') { if (c2 == '=') { tok = Token::AmpAmpEqual;         sourceOffset += 3; } else { tok = Token::AmpAmp;         sourceOffset += 2; } }
                                else if (c1 == '=') { tok = Token::AmpEqual;            sourceOffset += 2; } else { tok = Token::Amp;            sourceOffset += 1; } break; }
        case '|': { char c1 = sourceBuffer[sourceOffset + 1]; char c2 = sourceBuffer[sourceOffset + 2]; 
                    if (c1 == '|') { if (c2 == '=') { tok = Token::VertVertEqual;       sourceOffset += 3; } else { tok = Token::VertVert;       sourceOffset += 2; } }
                                else if (c1 == '=') { tok = Token::VertEqual;           sourceOffset += 2; } else { tok = Token::Vert;           sourceOffset += 1; } break; }
        case '>': { char c1 = sourceBuffer[sourceOffset + 1]; char c2 = sourceBuffer[sourceOffset + 2]; 
                    if (c1 == '>') { if (c2 == '=') { tok = Token::GreaterGreaterEqual; sourceOffset += 3; } else { tok = Token::GreaterGreater; sourceOffset += 2; } }
                                else if (c1 == '=') { tok = Token::GreaterEqual;        sourceOffset += 2; } else { tok = Token::Greater;        sourceOffset += 1; } break; }
        // clang-format on
        case '+': {
            char c1 = sourceBuffer[sourceOffset + 1];
            if (c1 == '+') {
                tok = Token::PlusPlus;
                sourceOffset += 2;
            } else if (c1 == '=') {
                tok = Token::PlusEqual;
                sourceOffset += 2;
            } else {
                tok = Token::Plus;
                sourceOffset += 1;
            }
            break;
        }
        case '-': {
            char c1 = sourceBuffer[sourceOffset + 1];
            if (c1 == '-') {
                tok = Token::MinusMinus;
                sourceOffset += 2;
            } else if (c1 == '=') {
                tok = Token::MinusEqual;
                sourceOffset += 2;
            } else if (c1 == '>') {
                tok = Token::Arrow;
                sourceOffset += 2;
            } else {
                tok = Token::Minus;
                sourceOffset += 1;
            }
            break;
        }
        case '=': {
            char c1 = sourceBuffer[sourceOffset + 1];
            if (c1 == '=') {
                tok = Token::EqualEqual;
                sourceOffset += 2;
            } else if (c1 == '>') {
                tok = Token::FatArrow;
                sourceOffset += 2;
            } else {
                tok = Token::Equal;
                sourceOffset += 1;
            }
            break;
        }
        case ':': {
            if (sourceBuffer[sourceOffset + 1] == ':') {
                tok = Token::ColonColon;
                sourceOffset += 2;
            } else {
                tok = Token::Colon;
                sourceOffset += 1;
            }
            break;
        }
        default: {
            HANDLE_LEXER_ACTION(errorHandler->invalidCharacter(this, c0));
        }
        }
        break;
    }
    tokenStream.emit(StreamToken::make(tok, tokBegin, sourceOffset));
    if (instrumenter)
        instrumenter->nextToken(this);
}

void Lexer::reemitLastToken(TokenWithData token) {
    VERIFY(cachedNextToken.tok == Token::Invalid);
    cachedNextToken = *this;
    (TokenWithData&)* this = token;
}