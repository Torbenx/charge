namespace parse {

const char* lexOld(const char* sourcePosition, std::vector<LexerToken>& output) {
    using Token = LexerToken;
    for (;;) {
        sourcePosition = skipWhitespace(sourcePosition);

        const char* tokBegin = sourcePosition;
        Token tok = Token::Invalid;

        char c0 = sourcePosition[0];
        switch (c0) {
        case '\0':
            tok = Token::EOS;
            return sourcePosition;
        case '\n': {
            sourcePosition += 1;
            continue;
        }
        case '\r': {
            if (sourcePosition[1] == '\n')
                sourcePosition += 2;
            else
                sourcePosition += 1;
            continue;
        }
        case '\'': {
            tok = Token::Literal;
            sourcePosition += 1;
            sourcePosition = skipToEndOfCharacterLiteral(sourcePosition);
            VERIFY(sourcePosition[0] == '\'');
            sourcePosition += 1;
            break;
        }
        case '/': {
            char c1 = sourcePosition[1];
            if (c1 == '/') {
                sourcePosition = skipToEndOfLine(sourcePosition);
                continue;
            } else if (c1 == '*') {
                sourcePosition = skipToEndOfBlockComment(sourcePosition);
                if (sourcePosition[0] == '\0') [[unlikely]] {
                    VERIFY_NOT_REACHED();
                }
                sourcePosition += 2;
                continue;
            } else if (c1 == '=') {
                tok = Token::SlashEqual;
                sourcePosition += 2;
            } else {
                tok = Token::Slash;
                sourcePosition += 1;
            }
            break;
        }
        case '<': {
            char c1 = sourcePosition[1];
            char c2 = sourcePosition[2];
            if (c1 == '=') {
                if (c2 == '>') {
                    tok = Token::LessEqualGreater;
                    sourcePosition += 3;
                } else {
                    tok = Token::LessEqual;
                    sourcePosition += 2;
                }
            } else if (c1 == '<') {
                if (c2 == '=') {
                    tok = Token::LessLessEqual;
                    sourcePosition += 3;
                } else {
                    tok = Token::LessLess;
                    sourcePosition += 2;
                }
            } else {
                tok = Token::Less;
                sourcePosition += 1;
            }
            break;
        }
            // clang-format off
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
        case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M':
        case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
        case '_': case '$': case '#': {
            do {
                sourcePosition += 1;
            } while (isWordBulkCharacter(sourcePosition[0]));
            const auto* entry = KeywordTable::get(tokBegin, sourcePosition - tokBegin);
            tok = entry == nullptr ? Token::Identifier : entry->token;
            break;
        }
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': {
            tok = Token::Literal;
            // TODO: implement parsing num literals
            for (;;) {
                char c = sourcePosition[0];
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')
                    sourcePosition += 1;
                else
                    break;
            }
            break;
        }
        // always one character punctuations
        case '(': { tok = Token::LeftParen;   sourcePosition += 1; break; }
        case ')': { tok = Token::RightParen;  sourcePosition += 1; break; }
        case '[': { tok = Token::LeftSquare;  sourcePosition += 1; break; }
        case ']': { tok = Token::RightSquare; sourcePosition += 1; break; }
        case '{': { tok = Token::LeftBrace;   sourcePosition += 1; break; }
        case '}': { tok = Token::RightBrace;  sourcePosition += 1; break; }
        case ',': { tok = Token::Comma;       sourcePosition += 1; break; }
        case '.': { tok = Token::Point;       sourcePosition += 1; break; }
        case '~': { tok = Token::Tilde;       sourcePosition += 1; break; }
        case ';': { tok = Token::SemiColon;   sourcePosition += 1; break; }
        // only followed by equal
        case '!': { if (sourcePosition[1] == '=') { tok = Token::ExclaimEqual; sourcePosition += 2; } else { tok = Token::Exclaim; sourcePosition += 1; } break; }
        case '*': { if (sourcePosition[1] == '=') { tok = Token::StarEqual;    sourcePosition += 2; } else { tok = Token::Star;    sourcePosition += 1; } break; }
        case '^': { if (sourcePosition[1] == '=') { tok = Token::HatEqual;     sourcePosition += 2; } else { tok = Token::Hat;     sourcePosition += 1; } break; }
        case '%': { if (sourcePosition[1] == '=') { tok = Token::PercentEqual; sourcePosition += 2; } else { tok = Token::Percent; sourcePosition += 1; } break; }
        // & | >
        case '&': { char c1 = sourcePosition[1]; char c2 = sourcePosition[2];
                    if (c1 == '&') { if (c2 == '=') { tok = Token::AmpAmpEqual;         sourcePosition += 3; } else { tok = Token::AmpAmp;         sourcePosition += 2; } }
                                else if (c1 == '=') { tok = Token::AmpEqual;            sourcePosition += 2; } else { tok = Token::Amp;            sourcePosition += 1; } break; }
        case '|': { char c1 = sourcePosition[1]; char c2 = sourcePosition[2];
                    if (c1 == '|') { if (c2 == '=') { tok = Token::VertVertEqual;       sourcePosition += 3; } else { tok = Token::VertVert;       sourcePosition += 2; } }
                                else if (c1 == '=') { tok = Token::VertEqual;           sourcePosition += 2; } else { tok = Token::Vert;           sourcePosition += 1; } break; }
        case '>': { char c1 = sourcePosition[1]; char c2 = sourcePosition[2];
                    if (c1 == '>') { if (c2 == '=') { tok = Token::GreaterGreaterEqual; sourcePosition += 3; } else { tok = Token::GreaterGreater; sourcePosition += 2; } }
                                else if (c1 == '=') { tok = Token::GreaterEqual;        sourcePosition += 2; } else { tok = Token::Greater;        sourcePosition += 1; } break; }
        // clang-format on
        case '+': {
            char c1 = sourcePosition[1];
            if (c1 == '+') {
                tok = Token::PlusPlus;
                sourcePosition += 2;
            } else if (c1 == '=') {
                tok = Token::PlusEqual;
                sourcePosition += 2;
            } else {
                tok = Token::Plus;
                sourcePosition += 1;
            }
            break;
        }
        case '-': {
            char c1 = sourcePosition[1];
            if (c1 == '-') {
                tok = Token::MinusMinus;
                sourcePosition += 2;
            } else if (c1 == '=') {
                tok = Token::MinusEqual;
                sourcePosition += 2;
            } else if (c1 == '>') {
                tok = Token::MinusGreater;
                sourcePosition += 2;
            } else {
                tok = Token::Minus;
                sourcePosition += 1;
            }
            break;
        }
        case '=': {
            char c1 = sourcePosition[1];
            if (c1 == '=') {
                tok = Token::EqualEqual;
                sourcePosition += 2;
            } else if (c1 == '>') {
                tok = Token::EqualGreater;
                sourcePosition += 2;
            } else {
                tok = Token::Equal;
                sourcePosition += 1;
            }
            break;
        }
        case ':': {
            if (sourcePosition[1] == ':') {
                tok = Token::ColonColon;
                sourcePosition += 2;
            } else {
                tok = Token::Colon;
                sourcePosition += 1;
            }
            break;
        }
        default:
            dbgln("{:.12}", sourcePosition);
            VERIFY_NOT_REACHED();
        }
        output.push_back(tok);
    }
}

}