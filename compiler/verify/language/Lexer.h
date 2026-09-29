#pragma once

#include <WordStringTable.h>

#include <string>
#include <utility>
#include <vector>

namespace verify::language {

inline constexpr ConstWordStringTable words {
    "active",
    "from",
    "store",
    "call",
    "jump",
    "branch",
    "phi",
    "nop",
    "true",
    "false",
    "and",
    "or",
    "load",
    "pre",
    "post",
    "prove",
    "clause",
    "by",
    "union",
    "intersection",
    "setminus",
    "mset",
    "iset",
    "incl_iset",
    "excl_iset",
    "path_iset",
    "exact_iset",
    "invariant",
#define TACTIC(name, snake_case) #snake_case,
#include <verify/ir/tactics.inc>
#define SORT(name, snake_case) #snake_case, #snake_case "_scalar",
#include <verify/ir/sorts.inc>
};

//! The mathematical symbols an operator may be written with
/*!
The source may spell an operator either way, the formatter always writes the symbol. A symbol
never appears inside a name, so reading one is a token of its own.
*/
namespace symbols {
    inline constexpr std::string_view AND = "\u2227"; // ∧
    inline constexpr std::string_view OR = "\u2228"; // ∨
    inline constexpr std::string_view NOT = "\u00ac"; // ¬
    inline constexpr std::string_view NOT_EQUAL = "\u2260"; // ≠
    inline constexpr std::string_view UNION = "\u222a"; // ∪
    inline constexpr std::string_view INTERSECTION = "\u2229"; // ∩
    inline constexpr std::string_view SET_MINUS = "\u2216"; // ∖
}

struct ParserException : std::exception {
    ParserException(std::string message)
        : message(std::move(message)) { }
    std::string message;

    const char* what() const noexcept override {
        return message.data();
    }
};

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
    LeftArrow,
    Exclaim,
    ExclaimEqual,
    Equal,
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

//! The tokens of a source file together with the words they refer to
/*!
The indentation of the source is turned into scope tokens: every file begins a scope, and each line
either begins a deeper one, continues the current one (carrying its label if it has one) or ends
the scopes it is not indented to.
*/
struct LexedFile {
    WordStringTable wordTable;
    std::vector<Token> tokens;
};

LexedFile lexFile(const char* source);

struct TokenStream {
    TokenStream* parent = nullptr;
    TokenStream* child = nullptr;
    const Token* token = nullptr;
    uint32_t inlineDepth = 0;
    bool invalid = false;

    const Token& tok() const { return *token; }
    TokenKind tokKind() const { return token->kind(); }

    static TokenStream makeRoot(const Token* stream) {
        return TokenStream(stream);
    }

    TokenStream(TokenStream& parent)
        : parent(&parent) {
        VERIFY(!parent.invalid);
        VERIFY(parent.tokKind() == TokenKind::BeginScope);
        token = parent.token + 1;
        parent.child = this;
    }

    ~TokenStream() {
        VERIFY(child == nullptr);
        if (tokKind() != TokenKind::EndScope)
            invalid = true;
        if (parent != nullptr) {
            VERIFY(parent->child == this);
            parent->child = nullptr;
            if (invalid) {
                parent->invalid = true;
            } else {
                parent->token = token;
                parent->advance();
            }
        }
    }

    void advanceWithNoScopeChanges() {
        VERIFY(child == nullptr);
        VERIFY(!invalid);
        token += 1;
    }

    void advance() {
        VERIFY(child == nullptr);
        VERIFY(!invalid);
        token += 1;
        for (;;) {
            if (tokKind() == TokenKind::BeginScope) {
                inlineDepth += 1;
                token += 1;
                continue;
            } else if (tokKind() == TokenKind::ContinueScope) {
                if (inlineDepth > 0) {
                    if (!tok().word().empty())
                        error("Invalid label location");
                    token += 1;
                    continue;
                }
            } else if (tokKind() == TokenKind::EndScope) {
                if (inlineDepth > 0) {
                    token += 1;
                    inlineDepth -= 1;
                    continue;
                }
            }
            break;
        }
    }

    [[noreturn]] void error(std::string message) {
        throw ParserException(std::move(message));
    }

private:
    explicit TokenStream(const Token* stream) // root constructor
        : parent(nullptr), child(nullptr), token(stream) { }
};

}
