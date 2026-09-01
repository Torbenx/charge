#include <parse/TokenBuffer.h>

#include <parse/parse_gen.h>

namespace parse {

TokenBuffer::TokenBuffer(padded_string_view source)
    : wordTable { words }, source(source) {
    reset();
}

std::string_view TokenBuffer::tokenSpelling(TokenInfo info) const {
    LexerToken lexToken = lexerToken(info.kind());
    // TODO: Support literals
    if (lexToken == LexerToken::Invalid) {
        return "";
    } else if (lexToken == LexerToken::Identifier) {
        return wordTable.view(info.data1<DataKind::Word>());
    } else {
        return fixedSpelling(lexToken);
    }
}

std::optional<TokenHandle> TokenBuffer::findPrecedingToken(SourceLocation location) const {
    auto nextTokenIt = std::upper_bound(tokens.begin(), tokens.end(), location);
    if (nextTokenIt == tokens.begin())
        return std::nullopt;
    auto it = std::prev(nextTokenIt);
    VERIFY(it->location() <= location);
    return toHandle(it);
}

std::vector<TokenHandle> TokenBuffer::findContainingTokens(SourceLocation location) const {
    auto nextIt = std::upper_bound(tokens.begin(), tokens.end(), location);
    std::vector<TokenHandle> result;
    for (; nextIt != tokens.begin() && *std::prev(nextIt) <= location; --nextIt) {
        auto it = std::prev(nextIt);
        if (it->lineIndex() != location.lineIndex())
            continue;
        int_t length = tokenSpelling(*it).length();
        if (length == 0)
            continue;
        if ((int_t)it->offsetInLine() + length >= (int_t)location.offsetInLine())
            result.push_back(toHandle(it));
    }
    return result;
}

SourceLocation TokenBuffer::findSourceLocation(const char* position) const {
    VERIFY(source.begin() <= position && position <= source.end());
    auto it = std::ranges::upper_bound(lines, position, std::less(), [](const LineInfo& info) { return info.begin; });
    VERIFY(it != lines.begin());
    return SourceLocation(0, it - lines.begin() - 1, position - std::prev(it)->begin);
}

}