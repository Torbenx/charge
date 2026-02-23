#pragma once

#include <PageBumpAllocator.h>
#include <parse/IdentifierTable.h>
#include <parse/Token.h>
#include <parse/parse_gen.h>

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
    bool operator==(const TokenHandle&) const = default;
};

struct TokenRange {
    TokenHandle begin;
    TokenHandle end;

    bool contains(TokenHandle tok) const { return begin <= tok && tok < end; }
};

struct TokenBuffer {
    PageBumpAllocator<TokenInfo> tokens;
    PageBumpAllocator<LineInfo> lines;
    PageBumpAllocator<WhitespaceInfo> whitespace;
    IdentifierTable wordTable { words };
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

    TokenHandle toHandle(const TokenInfo* ptr) const {
        int_t index = ptr - tokens.begin();
        VERIFY(index >= 0 && index < tokens.size());
        return { (uint32_t)index };
    }
    TokenInfo& token(TokenHandle handle) { return tokens[handle.id()]; }
    TokenInfo* tokenPtr(TokenHandle handle) { return &tokens[handle.id()]; }
    const TokenInfo& token(TokenHandle handle) const { return tokens[handle.id()]; }
    const TokenInfo* tokenPtr(TokenHandle handle) const { return &tokens[handle.id()]; }

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

    std::string_view tokenSpelling(TokenInfo info) const {
        LexerToken lexToken = lexerToken(info.kind());
        // TODO: Support literals
        if (lexToken == LexerToken::Identifier) {
            return wordTable.view(info.data1<DataKind::Word>());
        } else {
            return fixedSpelling(lexToken);
        }
    }

    std::string_view whitespaceSpelling(WhitespaceInfo info) const {
        return std::string_view(sourcePointer(info.location()), info.length);
    }

    TokenHandle currentToken() const {
        return { (uint32_t)tokens.size() };
    }

    //! Finds the last token starts at or before \p location
    std::optional<TokenHandle> findPrecedingToken(SourceLocation location) const {
        auto nextTokenIt = std::upper_bound(tokens.begin(), tokens.end(), location);
        if (nextTokenIt == tokens.begin())
            return std::nullopt;
        auto it = std::prev(nextTokenIt);
        VERIFY(it->location() <= location);
        return toHandle(it);
    }

    //! Finds the token containing \p location (if any)
    std::optional<TokenHandle> findContainingToken(SourceLocation location) const {
        auto handle = findPrecedingToken(location);
        if (!handle.has_value())
            return std::nullopt;
        TokenInfo candidate = token(handle.value());
        if (candidate.lineIndex() != location.lineIndex())
            return std::nullopt;
        VERIFY(location.offsetInLine() >= candidate.offsetInLine());
        auto candidateEnd = candidate.offsetInLine() + tokenSpelling(candidate).length();
        if (location.offsetInLine() < candidateEnd)
            return handle;
        else
            return std::nullopt;
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