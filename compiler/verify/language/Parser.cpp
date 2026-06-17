#include <verify/language/Parser.h>

namespace verify::language {

static const char* skipSpaces(const char* position) {
    while (position[0] == ' ')
        position += 1;
    return position;
}

static bool isBulkNameCharacter(char c) {
    return ('A' <= c && c <= 'Z')
        || ('a' <= c && c <= 'z')
        || ('0' <= c && c <= '9')
        || c == '_' || c == ':';
}

enum class TokenKind : uint8_t {
    BeginScope,
    ContinueScope,
    EndScope,

    Identifier,
    LocalName,
    TheoremName,
    LabelName,
    GlobalName,

    LeftParen,
    RightParen,
    Colon,
    Comma,
    Point,
};

struct Token {
    TokenKind m_kind;
    uint32_t m_data = 0;

    TokenKind kind() const { return m_kind; }

    Word word() const {
        VERIFY(kind() == TokenKind::Identifier
            || kind() == TokenKind::LocalName
            || kind() == TokenKind::TheoremName
            || kind() == TokenKind::LabelName
            || kind() == TokenKind::GlobalName
            || kind() == TokenKind::ContinueScope);
        return Word::fromUint(m_data);
    }
};

struct Lexer {
    void lex();
    Word readWord(const char*& position);

    struct ScopeStackEntry {
        uint32_t indent = 0;
    };

    WordStringTable wordTable;
    std::vector<Token> tokens;
    std::vector<ScopeStackEntry> scopeStack;
};

void Lexer::lex() {
    const char* position;

    for (;;) {
        position = skipSpaces(position);
        switch (position[0]) {
        case '\n': {
            position += 1;
            break;
        }
        case '\r': {
            if (position[1] == '\n')
                position += 2;
            else
                position += 1;
            break;
        }

        case '(':
            position += 1;
            tokens.push_back({ TokenKind::LeftParen });
            continue;
        case ')':
            position += 1;
            tokens.push_back({ TokenKind::RightParen });
            continue;
        case ':':
            position += 1;
            tokens.push_back({ TokenKind::Colon });
            continue;
        case ',':
            position += 1;
            tokens.push_back({ TokenKind::Comma });
            continue;
        case '.':
            position += 1;
            tokens.push_back({ TokenKind::Point });
            continue;

        case '$':
            position += 1;
            tokens.push_back({ TokenKind::LocalName, readWord(position).toUint() });
            continue;
        case '%':
            position += 1;
            tokens.push_back({ TokenKind::TheoremName, readWord(position).toUint() });
            continue;
        case '@':
            position += 1;
            tokens.push_back({ TokenKind::LabelName, readWord(position).toUint() });
            continue;
        case '#':
            position += 1;
            tokens.push_back({ TokenKind::GlobalName, readWord(position).toUint() });
            continue;
            // clang-format off
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M':
            case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
            case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
            case '_':
            // clang-format on
            tokens.push_back({ TokenKind::Identifier, readWord(position).toUint() });
            continue;
            // clang-format off
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            // clang-format on
            VERIFY_NOT_REACHED();

        default:
            VERIFY_NOT_REACHED();
        }

        // Handle scope changes
        const char* lineBegin;
        std::vector<Word> labels;
        for (;;) {
            lineBegin = position;
            if (position[0] == '@') {
                position += 1;
                labels.push_back(readWord(position));
                VERIFY(position[0] == ':');
            }
            position = skipSpaces(position);
            if (position[0] == '\r') {
                if (position[1] == '\n')
                    position += 2;
                else
                    position += 1;
                continue;
            } else if (position[0] == '\n') {
                position += 1;
                continue;
            }
            break;
        }
        uint32_t indent = position - lineBegin;
        if (indent > scopeStack.back().indent) {
            tokens.push_back({ TokenKind::BeginScope });
            scopeStack.push_back({ .indent = indent });
            for (Word label : labels)
                tokens.push_back({ TokenKind::ContinueScope, label.toUint() });
        } else {
            while (indent < scopeStack.back().indent) {
                tokens.push_back({ TokenKind::EndScope });
                scopeStack.pop_back();
            }
            VERIFY(indent == scopeStack.back().indent);
            if (labels.empty()) {
                tokens.push_back({ TokenKind::ContinueScope });
            } else {
                for (Word label : labels)
                    tokens.push_back({ TokenKind::ContinueScope, label.toUint() });
            }
        }
    }
}

[[gnu::always_inline]] Word Lexer::readWord(const char*& position) {
    const char* begin = position;
    Word::HashState state;
    while (isBulkNameCharacter(*position)) {
        Word::iterateHash(state, *position);
        position += 1;
    }
    auto hash = Word::finalizeHash(state, position - begin);
    return wordTable.getWithHash({ begin, position }, hash);
}

struct TokenStream {
    TokenStream* parent = nullptr;
    TokenStream* child = nullptr;
    Token* token = nullptr;
    uint32_t inlineDepth = 0;

    const Token& tok() const { return *token; }
    TokenKind tokKind() const { return token->kind(); }

    TokenStream(TokenStream& parent)
        : parent(&parent) {
        VERIFY(parent.tokKind() == TokenKind::BeginScope);
        token = parent.token + 1;
        parent.child = this;
    }

    ~TokenStream() {
        VERIFY(child == nullptr);
        if (parent != nullptr) {
            VERIFY(parent->child == this);
            VERIFY(tokKind() == TokenKind::EndScope);
            parent->token = token;
            parent->child = nullptr;
            parent->advance();
        }
    }

    void consumeScopeInline() {
        VERIFY(child == nullptr);
        VERIFY(tokKind() == TokenKind::BeginScope);
        token += 1;
        inlineDepth += 1;
    }

    void advance() {
        VERIFY(child == nullptr);
        token += 1;
        if (inlineDepth > 0) {
            for (;;) {
                if (tokKind() == TokenKind::ContinueScope) {
                    if (!tok().word().empty())
                        error("Invalid label location");
                    token += 1;
                    continue;
                } else if (tokKind() == TokenKind::EndScope) {
                    token += 1;
                    inlineDepth -= 1;
                    if (inlineDepth > 0)
                        continue;
                }
                break;
            }
        }
    }

    void error(std::string message) {
        throw ParserException(std::move(message));
    }
};

struct Parser {

    void parseTopLevel(TokenStream s) {
        if (s.tokKind() == TokenKind::Identifier && s.tok().word() == words["fn"]) {
            // Parse function
            s.advance();
            if (s.tokKind() != TokenKind::GlobalName)
                s.error("Expected global name after 'fn'");
            // FnBuilder fn(db);
            Word fnName = s.tok().word();
            s.advance();
            int_t parameterCount = 0;
            for (;;) {
                if (s.tokKind() != TokenKind::LocalName)
                    s.error("Expected parameter name");
                locals.insert(s.tok().word(), Local(LocalKind::Parameter, parameterCount));
                s.advance();
                parameterCount += 1;
                if (s.tokKind() == TokenKind::Comma) {
                    s.advance();
                    continue;
                } else if (s.tokKind() == TokenKind::RightParen) {
                    s.advance();
                    break;
                } else {
                    s.error("Unexpected token after parameter");
                }
            }

            if (s.tokKind() != TokenKind::Colon)
                s.error("Expected ':' after function parameters");
            s.advance();
            if (s.tokKind() != TokenKind::BeginScope)
                s.error("Expected function body after ':'");
            parseInstructions(s);
        } else {
            s.error("Unexpected token at top level");
        }
    }

    void parseInstructions(TokenStream s) {
        for (;;) {
            switch (s.tokKind()) {
            case TokenKind::EndScope:
                return;
            case TokenKind::ContinueScope:
                // TODO if (!s.tok().word().empty())
                //     labels.insert(s.tok().word(), );
                break;
            case TokenKind::Identifier:
                break;
            default:
                s.error("Expected instruction");
            }
            s.advance();
        }
    }

    void parseExpression(TokenStream& s) {
        for (;;) {
            switch (s.tokKind()) {
            case TokenKind::BeginScope:
                s.consumeScopeInline();
                continue;
            case TokenKind::ContinueScope:
            case TokenKind::EndScope:
                return;
            case TokenKind::LabelName: {
                ir::CodePos labelPos = getLabel(s);
                s.advance();
                if (s.tokKind() != TokenKind::Point)
                    s.error("Expected '.' after label");
                s.advance();
                if (s.tokKind() != TokenKind::Identifier)
                    s.error("Expected identifier after label");
                if (s.tok().word() == words["active"]) {

                } else if (s.tok().word() == words["from"]) {

                } else {
                    s.error("Invalid identifier after label");
                }
            }
            case TokenKind::GlobalName:
            case TokenKind::LocalName:
                break;
            }
        }
    }

    ir::CodePos getLabel(TokenStream& s) const {
        VERIFY(s.tokKind() == TokenKind::LabelName);
        auto maybeResult = labels.get(s.tok().word());
        if (!maybeResult.has_value())
            s.error("Invalid label");
        return maybeResult.value();
    }

    ir::Database& db;
    LookupTable<ir::Theorem> theorems;
    LookupTable<ir::CodePos> labels;
    LookupTable<Local> locals;
};

}