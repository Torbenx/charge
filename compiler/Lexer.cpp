#include "Parser.h"
#include <array>

std::string_view toSmallString(Token tok) {
    using enum Token;
    // clang-format off
    switch (tok) {
    case Invalid: return "inval";

    case LeftParen: return "(";
    case RightParen: return ")";
    case LeftSquare: return "[";
    case RightSquare: return "]";
    case LeftBrace: return "{";
    case RightBrace: return "}";

    case Exclaim: return "!";
    case Tilde: return "~";
    case PlusPlus: return "++";
    case MinusMinus: return "--";
    case Plus: return "+";
    case Minus: return "-";
    case Amp: return "&";
    case AmpAmp: return "&&";
    case Hat: return "^";
    case Vert: return "|";
    case VertVert: return "||";
    case Star: return "*";
    case Slash: return "/";
    case Percent: return "%";
    case LessLess: return "<<";
    case GreaterGreater: return ">>";
    case ExclaimEqual: return "!=";
    case EqualEqual: return "==";
    case Less: return "<";
    case LessEqual: return "<=";
    case Greater: return ">";
    case GreaterEqual: return ">=";
    case PlusEqual: return "+=";
    case MinusEqual: return "-=";
    case AmpEqual: return "&=";
    case AmpAmpEqual: return "&&=";
    case HatEqual: return "^=";
    case VertEqual: return "|=";
    case VertVertEqual: return "||=";
    case StarEqual: return "*=";
    case SlashEqual: return "/=";
    case PercentEqual: return "%=";
    case LessLessEqual: return "<<=";
    case GreaterGreaterEqual: return ">>=";

    case Equal: return "=";
    case Question: return "?";
    case Comma: return ",";
    case Point: return ".";
    case Colon: return ":";
    case ColonColon: return "::";
    case SemiColon: return ";";
    case FatArrow: return "=>";
    case DoubleArrow: return "<=>";
    case Arrow: return "->";
    case Word: return "word";
    case NumericLiteral: return "num";
    case CharacterLiteral: return "char";

    case LineComment: return "//";
    case BlockComment: return "/>";

    case Newline: return "newline";

    case EOS: return "EOS";
    case COUNT:
        VERIFY_NOT_REACHED();
    }
    // clang-format on
}
std::string_view nameString(Token token) {
    switch (token) {
    case Token::Invalid:
        return "Invalid";
    case Token::COUNT:
        VERIFY_NOT_REACHED();

#define TOKEN(t)   \
    case Token::t: \
        return #t;

        ENUMERTATE_TOKENS

#undef token
    }
}

// advances offset to the next non-whitespace character
static void skipTabsAndSpaces(Lexer& lex) {
    while (lex.sourceBuffer[lex.sourceOffset] == ' ' || lex.sourceBuffer[lex.sourceOffset] == '\t')
        lex.sourceOffset += 1;
}

static bool isEndOfLineCharacter(uint8_t c) {
    return c == '\n' || c == '\r';
}
// advances offset to the next new line character
static void skipToEndOfLine(Lexer& lex) {
    while (!isEndOfLineCharacter(lex.sourceBuffer[lex.sourceOffset]))
        lex.sourceOffset += 1;
}

// advances offset to the next '</'
static void skipToEndOfBlockComment(Lexer& lex) {
    while (lex.sourceBuffer[lex.sourceOffset] != '\0'
        && !(lex.sourceBuffer[lex.sourceOffset] == '<' && lex.sourceBuffer[lex.sourceOffset + 1] == '/')) {
        lex.sourceOffset += 1;
    }
}

static void skipToEndOfCharacterLiteral(Lexer& lex) {
    for (;;) {
        auto c = lex.sourceBuffer[lex.sourceOffset];
        if (c == '\'' || c == '\n' || c == '\r' || c == '\0')
            break;
        lex.sourceOffset += 1;
    }
}

static bool isBulkWordCharacter(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

namespace lookup_table1 {

struct Input {
    Token bare;
    Token repeat = bare;
    Token equal = bare;
    Token arrow = bare;
    Token repeatEqual = repeat;
    Token repeatArrow = repeat;
    Token equalArrow = equal;
    Token repeatEqualArrow = repeatEqual;
};
struct InputRange : Input {
    uint8_t first, last;
    constexpr InputRange(char target, Input input)
        : Input(input), first(target), last(target) { }
    constexpr InputRange(uint8_t first, uint8_t last, Input input)
        : Input(input), first(first), last(last) { }
};

struct Output {
    static_assert(std::to_underlying(Token::COUNT) <= 1 << 6);
    uint8_t token : 6 = 0;
    uint8_t advance : 2 = 0;
    constexpr Output() = default;
    constexpr Output(Token token, int_t advance)
        : token(std::to_underlying(token)), advance(advance) {
        VERIFY(advance < 4);
    }
};

static constexpr int_t getOffset(uint8_t character, bool repeat, bool equal, bool arrow) {
    VERIFY(character >= 0x20 && character < 0x40);
    return (character - 0x20) | (repeat << 5) | (equal << 6) | (arrow << 7);
}

static constexpr auto makeTable(std::initializer_list<InputRange> inputs) {
    std::array<Output, 0x20 * 8> output = {};
    for (auto input : inputs) {
        for (uint8_t c = input.first; c <= input.last; c++) {
            int_t bareAdvance = 1;
            output[getOffset(c, false, false, false)] = { input.bare, 1 };
            int_t equalAdvance = bareAdvance + (input.equal == input.bare ? 0 : 1);
            output[getOffset(c, false, true, false)] = { input.equal, equalAdvance };
            output[getOffset(c, false, false, true)] = { input.arrow, bareAdvance + (input.arrow == input.bare ? 0 : 1) };
            output[getOffset(c, false, true, true)] = { input.equalArrow, equalAdvance + (input.equalArrow == input.equal ? 0 : 1) };
            int_t repeatAdvance = input.repeat == input.bare ? bareAdvance : bareAdvance + 1;
            output[getOffset(c, true, false, false)] = { input.repeat, repeatAdvance };
            int_t repeatEqualAdvance = repeatAdvance + (input.repeatEqual == input.repeat ? 0 : 1);
            output[getOffset(c, true, true, false)] = { input.repeatEqual, repeatEqualAdvance };
            output[getOffset(c, true, false, true)] = { input.repeatArrow, repeatAdvance + (input.repeatArrow == input.repeat ? 0 : 1) };
            output[getOffset(c, true, true, true)] = { input.repeatEqualArrow, repeatEqualAdvance + (input.repeatEqualArrow == input.repeatEqual ? 0 : 1) };
        }
    }
    return output;
}

static constexpr auto table = makeTable({
    { '(', { Token::LeftParen } },
    { ')', { Token::RightParen } },
    { '?', { Token::Question } },
    { ',', { Token::Comma } },
    { '.', { Token::Point } },
    { ';', { Token::SemiColon } },
    { ':', { .bare = Token::Colon, .repeat = Token::ColonColon } },

    { '!', { .bare = Token::Exclaim, .equal = Token::ExclaimEqual } },
    { '%', { .bare = Token::Percent, .equal = Token::PercentEqual } },
    { '*', { .bare = Token::Star, .equal = Token::StarEqual } },

    { '/', { .bare = Token::Slash, .repeat = Token::LineComment, .equal = Token::SlashEqual, .arrow = Token::BlockComment } },

    { '=', { .bare = Token::Equal, .repeat = Token::EqualEqual, .arrow = Token::FatArrow } },

    { '+', { .bare = Token::Plus, .repeat = Token::PlusPlus, .equal = Token::PlusEqual } },
    { '-', { .bare = Token::Minus, .repeat = Token::MinusMinus, .equal = Token::MinusEqual, .arrow = Token::Arrow } },

    { '&', { .bare = Token::Amp, .repeat = Token::AmpAmp, .equal = Token::AmpEqual, .repeatEqual = Token::AmpAmpEqual } },

    { '<', { .bare = Token::Less, .repeat = Token::LessLess, .equal = Token::LessEqual, .repeatEqual = Token::LessLessEqual, .equalArrow = Token::DoubleArrow } },
    { '>', { .bare = Token::Greater, .repeat = Token::GreaterGreater, .equal = Token::GreaterEqual, .repeatEqual = Token::GreaterGreaterEqual } },

    { '0', '9', { Token::NumericLiteral } },
    { '\'', { Token::CharacterLiteral } },
    { '$', { Token::Word } },
    { '#', { Token::Word } },
});

static constexpr std::pair<Token, int_t> lookup(uint8_t character, bool repeat, bool equal, bool arrow) {
    Output out = table[getOffset(character, repeat, equal, arrow)];
    return { (Token)out.token, (int_t)out.advance };
}

}

namespace lookup_table2 {

struct Input {
    uint8_t first;
    uint8_t last;
    Token token;
    constexpr Input(uint8_t character, Token token)
        : first(character), last(character), token(token) { }
    constexpr Input(uint8_t first, uint8_t last, Token token)
        : first(first), last(last), token(token) { }
};

struct Output {
    Token token : 8 = Token::Invalid;
};

static constexpr int_t getOffset(uint8_t character) {
    VERIFY(character >= 0x40 && character < 0x80);
    return character - 0x40;
}

static constexpr auto makeTable(std::initializer_list<Input> inputs) {
    std::array<Output, 0x40> output = {};
    for (auto input : inputs) {
        for (uint8_t c = input.first; c <= input.last; c++) {
            output[getOffset(c)] = Output { input.token };
        }
    }
    return output;
}

static constexpr auto table = makeTable({
    { 'a', 'z', Token::Word },
    { 'A', 'Z', Token::Word },
    { '_', Token::Word },
    { '[', Token::LeftSquare },
    { ']', Token::RightSquare },
    { '{', Token::LeftBrace },
    { '}', Token::RightBrace },
    { '~', Token::Tilde },
});

static constexpr Token lookup(uint8_t character) { return table[getOffset(character)].token; }

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
    return std::holds_alternative<std::nullopt_t>(tokData);
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
        skipTabsAndSpaces(*this);

        tokBegin = sourceOffset;
        tok = Token::Invalid;
        tokData = std::nullopt;
        auto head = sourceBuffer[sourceOffset];
        if (head == '\0' && sourceOffset == sourceBuffer.length()) {
            tok = Token::EOS;
            break;
        }
        if (head == '\n') {
            tok = Token::Newline;
            lineNumber += 1;
            sourceOffset += 1;
            tokenStream.emit(StreamToken::makeNewline(lineNumber, sourceOffset));
            continue;
        }
        if (head == '\r' && sourceBuffer[sourceOffset + 1] == '\n') {
            tok = Token::Newline;
            lineNumber += 1;
            sourceOffset += 2;
            tokenStream.emit(StreamToken::makeNewline(lineNumber, sourceOffset));
            continue;
        }
        int_t advance = 0;
        if (head <= 0x20 || head >= 0x7f) [[unlikely]] {
            HANDLE_LEXER_ACTION(errorHandler->invalidCharacter(this, sourceBuffer[sourceOffset]));
        } else if (head < 0x40) {
            bool repeat = sourceBuffer[sourceOffset + 1] == head;
            bool equal = sourceBuffer[sourceOffset + 1 + (repeat ? 1 : 0)] == '=';
            bool arrow = sourceBuffer[sourceOffset + 1 + (repeat ? 1 : 0) + (equal ? 1 : 0)] == '>';
            std::tie(tok, advance) = lookup_table1::lookup(head, repeat, equal, arrow);
            if (tok == Token::Invalid) [[unlikely]] {
                HANDLE_LEXER_ACTION(errorHandler->invalidCharacter(this, sourceBuffer[sourceOffset]));
            }
        } else if (head == '|') {
            bool repeat = sourceBuffer[sourceOffset + 1] == '|';
            bool equal = sourceBuffer[sourceOffset + 1 + (repeat ? 1 : 0)] == '=';
            std::tie(tok, advance) = repeat
                ? (equal ? std::make_tuple(Token::VertVertEqual, 3) : std::make_tuple(Token::VertVert, 2))
                : (equal ? std::make_tuple(Token::VertEqual, 2) : std::make_tuple(Token::Vert, 1));
        } else if (head == '^') {
            std::tie(tok, advance) = sourceBuffer[sourceOffset + 1] == '='
                ? std::make_tuple(Token::HatEqual, 2)
                : std::make_tuple(Token::Hat, 1);
        } else {
            tok = lookup_table2::lookup(head);
            advance = 1;
            if (tok == Token::Invalid) [[unlikely]] {
                HANDLE_LEXER_ACTION(errorHandler->invalidCharacter(this, sourceBuffer[sourceOffset]));
            }
        }

        if (tok == Token::Word) {
            uint32_t hash = 0;
            do {
                hash = Word::iterateHash(hash, sourceBuffer[sourceOffset]);
                sourceOffset += 1;
            } while (isBulkWordCharacter(sourceBuffer[sourceOffset]));
            hash = Word::finalizeHash(hash);
            tokData = wordTable.getWithHash(source(tokBegin, sourceOffset), hash);
        } else if (tok == Token::NumericLiteral) {
            // TODO: implement parsing num literals
            for (;;) {
                char c = sourceBuffer[sourceOffset];
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')
                    sourceOffset += 1;
                else
                    break;
            }
            tokData = NumericLiteral();
        } else if (tok == Token::CharacterLiteral) {
            sourceOffset += 1;
            skipToEndOfCharacterLiteral(*this);
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
        } else if (tok == Token::LineComment) {
            skipToEndOfLine(*this);
            tokenStream.emit(StreamToken::make(tok, tokBegin, sourceOffset));
            if (instrumenter)
                instrumenter->handleComment(this);
            continue;
        } else if (tok == Token::BlockComment) {
            skipToEndOfBlockComment(*this);
            if (sourceBuffer[sourceOffset] == '\0') [[unlikely]] {
                HANDLE_LEXER_ACTION(errorHandler->unterminatedBlockComment(this, tokBegin));
            }
            sourceOffset += 2;
            tokenStream.emit(StreamToken::make(tok, tokBegin, sourceOffset));
            if (instrumenter)
                instrumenter->handleComment(this);
            continue;
        } else {
            sourceOffset += advance;
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