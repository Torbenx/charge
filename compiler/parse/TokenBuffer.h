#pragma once

#include <PageBumpAllocator.h>
#include <parse/parse_gen.h>
#include <parse/Token.h>

#include <utility>
#include <vector>

namespace parse {

struct LineInfo {
    const char* begin = nullptr;
};

enum class WhitespaceKind : uint8_t {
    LineComment,
    BlockComment,
    EOS,
};

struct WhitespaceInfo : TaggedSourceLocation<WhitespaceKind> {
    uint32_t length = 0;
};

struct TokenHandle {
    uint32_t m_id = -1;

    constexpr uint32_t id() const { return m_id; }

    auto operator<=>(const TokenHandle&) const = default;
};

struct TokenBuffer {
    PageBumpAllocator<TokenInfo> tokens;
    PageBumpAllocator<LineInfo> lines;
    PageBumpAllocator<WhitespaceInfo> whitespace;
    std::vector<Word> callArguments;
    std::string_view source;
    TokenBuffer(std::string_view source)
        : source(source) {
        reset();
    }

    void reset() {
        tokens.clear();
        lines.clear();
        whitespace.clear();
        lines.push_back({ source.data() });
    }

    TokenInfo& token(TokenHandle handle) { return tokens[handle.id()]; }
    TokenInfo* tokenPtr(TokenHandle handle) { return &tokens[handle.id()]; }

    CallArgumentsHandle addCallArguments(std::span<const Word> arguments) {
        CallArgumentsHandle handle { (uint32_t)callArguments.size() };
        callArguments.push_back(Word::fromUint(arguments.size()));
        callArguments.insert(callArguments.end(), arguments.begin(), arguments.end());
        return handle;
    }

    std::span<const Word> argumentNames(CallArgumentsHandle handle) {
        int_t count = std::bit_cast<uint32_t>(callArguments[handle.offset]);
        return std::span<const Word>(callArguments.data() + handle.offset + 1, count);
    }

    const char* sourcePointer(SourceLocation loc) const {
        return lines[loc.lineIndex()].begin + loc.offsetInLine();
    }

    std::string_view whitespaceSpelling(WhitespaceInfo info) const {
        return std::string_view(sourcePointer(info.location()), info.length);
    }

    TokenHandle currentToken() const {
        return { (uint32_t)tokens.size() };
    }

    std::optional<TokenInfo*> findToken(SourceLocation location) {
        auto compare = [](const TokenInfo& token, SourceLocation location) {
            return token.location() < location;
        };
        auto it = std::lower_bound(tokens.begin(), tokens.end(), location, compare);
        if (it == tokens.begin())
            return it;
        return std::prev(it);
    }
};

template<typename Impl>
struct MergedTokenVisitor {
    Impl* impl() { return static_cast<Impl*>(this); }

    void visit(const TokenBuffer& buffer) {
        auto tokenIt = buffer.tokens.begin();
        auto whitespaceIt = buffer.whitespace.begin();
        for (;;) {
            auto result = *tokenIt <=> *whitespaceIt;
            if (result < 0) {
                impl()->visitToken(*tokenIt);
                tokenIt += 1;
            } else if (result > 0) {
                impl()->visitWhitespace(*whitespaceIt);
                whitespaceIt += 1;
            } else {
                VERIFY(tokenIt + 1 == buffer.tokens.end() && whitespaceIt + 1 == buffer.whitespace.end());
                break;
            }
        }
    }
};

}

template<>
struct optional_traits<parse::TokenHandle> {
    static constexpr parse::TokenHandle empty_value = {};
};