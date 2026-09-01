#pragma once

#include <WordStringTable.h>

namespace parse {

inline constexpr size_t KEYWORD_WORD_ID = 0;
inline constexpr size_t SPECIAL_IDENTIFIER_WORD_ID = 1;
inline constexpr size_t FIRST_REGULAR_IDENTIFIER_WORD_ID = 2;

constexpr bool isKeyword(Word word) { return word.id() == KEYWORD_WORD_ID; }
constexpr bool isSpecialIdentifier(Word word) { return word.id() == SPECIAL_IDENTIFIER_WORD_ID; }

struct IdentifierTable : WordStringTable {
    template<typename... Ts>
    constexpr IdentifierTable(const ConstWordStringTable<Ts...>& s)
        : WordStringTable(s) { }

    template<StringViewLike S>
    constexpr Word get(S str) { return getWithHash(str, Word::hash(str)); }
    template<StringViewLike S>
    constexpr Word getWithHash(S str, uint32_t hash) {
        return getInIdRange(str, hash, FIRST_REGULAR_IDENTIFIER_WORD_ID, Word::MAX_ID + 1);
    }
};

}