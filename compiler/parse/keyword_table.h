/* C++ code produced by gperf version 3.3 */
/* Command-line: gperf --output=keyword_table.h -m100 ./keyword_table.gperf  */
/* Computed positions: -k'1,$' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif

#line 2 "./keyword_table.gperf"

#pragma once
#include <padded_string_compare.h>
#include <parse/parse_gen.h>
#include <algorithm>
#include <array>
namespace parse {

struct GPerfFixedString {
    static constexpr size_t MAX_KEYWORD_LENGTH = 10;
    // The keyword characters are compared as one whole padding block, so give the storage
    // that size. Rounding up also keeps the LexerToken behind it out of the compared bytes,
    // which would otherwise turn the load of the token into a store forwarding round trip
    // through the comparison.
    static constexpr size_t STORAGE_SIZE = 16;

    template<size_t N>
    consteval GPerfFixedString(const char (&str)[N]) {
        static_assert(N <= MAX_KEYWORD_LENGTH + 1);
        storage.fill(0);
        std::copy_n(str, N, storage.data());
    }
    const char& operator*() const { return storage[0]; }
    operator const char*() const { return storage.data(); }
    std::array<char, MAX_KEYWORD_LENGTH + 1> storage;
};
static_assert(GPerfFixedString::MAX_KEYWORD_LENGTH < GPerfFixedString::STORAGE_SIZE);
// The keyword side of a comparison is loaded as one whole padding block
static_assert(GPerfFixedString::STORAGE_SIZE >= PADDED_STRING_PADDING);

// The gperf generated KeywordTable::get() compares a candidate against a keyword with
//     *str == *s && !memcmp(str + 1, s + 1, len - 1)
// Unqualified lookup from inside namespace parse finds this overload first and never
// reaches ::memcmp, which contains an over-conservative page crossing check. We can
// avoid this because of the added padding.
inline int memcmp(const char* candidate, const char* keyword, size_t n) {
    // Undo the offset that gperf applies to both pointers. Comparing the first character
    // again is free, it sits in the same block, and it is what puts the keyword side on the
    // aligned start of the entry.
    return padded_small_string_compare_eq(candidate - 1, keyword - 1, (int_t)n + 1) ? 0 : 1;
}

#line 61 "./keyword_table.gperf"
struct KeywordTableEntry { struct alignas(std::bit_ceil(sizeof(GPerfFixedString) + 1)) { GPerfFixedString string; LexerToken token; }; };
enum
  {
    KEYWORD_TABLE_TOTAL_KEYWORDS = 35,
    KEYWORD_TABLE_MIN_WORD_LENGTH = 2,
    KEYWORD_TABLE_MAX_WORD_LENGTH = 10,
    KEYWORD_TABLE_MIN_HASH_VALUE = 3,
    KEYWORD_TABLE_MAX_HASH_VALUE = 37
  };

/* maximum key range = 35, duplicates = 0 */

class KeywordTable
{
private:
  static inline unsigned int hash (const char *str, size_t len);
public:
  static const struct KeywordTableEntry *get (const char *str, size_t len);
};

inline unsigned int
KeywordTable::hash (const char *str, size_t len)
{
  static const unsigned char asso_values[] =
    {
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
      38, 38, 38, 38, 38, 38, 38, 27, 28,  7,
       8,  0,  1, 38, 22,  4, 38,  0,  3, 27,
       0, 25, 21, 38, 18,  7,  4, 24, 15,  3,
      38, 12, 38, 38, 38, 38, 38, 38
    };
  return len + asso_values[static_cast<unsigned char>(str[len - 1])] + asso_values[static_cast<unsigned char>(str[0])];
}

static const unsigned char KEYWORD_TABLE_LENGTHS[] =
  {
     0,  0,  0,  2,  4,  4,  2,  2,  5,  9,  3,  4,  8,  5,
    10,  8,  5,  6,  7,  3,  6,  6,  3,  7,  6,  7,  5,  7,
     4,  4,  6,  4,  4,  5,  5,  2,  3,  6
  };

static const struct KeywordTableEntry KEYWORD_TABLE_ENTRIES[] =
  {
    {"",LexerToken::Identifier}, {"",LexerToken::Identifier},
    {"",LexerToken::Identifier},
#line 91 "./keyword_table.gperf"
    {"fn",LexerToken::Fn},
#line 73 "./keyword_table.gperf"
    {"else",LexerToken::Else},
#line 72 "./keyword_table.gperf"
    {"elif",LexerToken::Elif},
#line 77 "./keyword_table.gperf"
    {"in",LexerToken::In},
#line 75 "./keyword_table.gperf"
    {"if",LexerToken::If},
#line 87 "./keyword_table.gperf"
    {"while",LexerToken::While},
#line 93 "./keyword_table.gperf"
    {"namespace",LexerToken::Namespace},
#line 78 "./keyword_table.gperf"
    {"let",LexerToken::Let},
#line 76 "./keyword_table.gperf"
    {"impl",LexerToken::Impl},
#line 96 "./keyword_table.gperf"
    {"template",LexerToken::Template},
#line 97 "./keyword_table.gperf"
    {"trait",LexerToken::Trait},
#line 92 "./keyword_table.gperf"
    {"incomplete",LexerToken::Incomplete},
#line 68 "./keyword_table.gperf"
    {"continue",LexerToken::Continue},
#line 67 "./keyword_table.gperf"
    {"const",LexerToken::Const},
#line 95 "./keyword_table.gperf"
    {"struct",LexerToken::Struct},
#line 89 "./keyword_table.gperf"
    {"context",LexerToken::Context},
#line 84 "./keyword_table.gperf"
    {"try",LexerToken::Try},
#line 83 "./keyword_table.gperf"
    {"static",LexerToken::Static},
#line 82 "./keyword_table.gperf"
    {"shared",LexerToken::Shared},
#line 74 "./keyword_table.gperf"
    {"for",LexerToken::For},
#line 70 "./keyword_table.gperf"
    {"discard",LexerToken::Discard},
#line 81 "./keyword_table.gperf"
    {"return",LexerToken::Return},
#line 98 "./keyword_table.gperf"
    {"virtual",LexerToken::Virtual},
#line 80 "./keyword_table.gperf"
    {"prove",LexerToken::Prove},
#line 69 "./keyword_table.gperf"
    {"destroy",LexerToken::Destroy},
#line 79 "./keyword_table.gperf"
    {"loop",LexerToken::Loop},
#line 94 "./keyword_table.gperf"
    {"open",LexerToken::Open},
#line 85 "./keyword_table.gperf"
    {"unique",LexerToken::Unique},
#line 90 "./keyword_table.gperf"
    {"enum",LexerToken::Enum},
#line 88 "./keyword_table.gperf"
    {"base",LexerToken::Base},
#line 65 "./keyword_table.gperf"
    {"break",LexerToken::Break},
#line 66 "./keyword_table.gperf"
    {"catch",LexerToken::Catch},
#line 71 "./keyword_table.gperf"
    {"do",LexerToken::Do},
#line 86 "./keyword_table.gperf"
    {"var",LexerToken::Var},
#line 64 "./keyword_table.gperf"
    {"assert",LexerToken::Assert}
  };

const struct KeywordTableEntry *
KeywordTable::get (const char *str, size_t len)
{
  if (len <= KEYWORD_TABLE_MAX_WORD_LENGTH && len >= KEYWORD_TABLE_MIN_WORD_LENGTH)
    {
      unsigned int key = hash (str, len);

      if (key <= KEYWORD_TABLE_MAX_HASH_VALUE)
        if (len == KEYWORD_TABLE_LENGTHS[key])
          {
            const char *s = KEYWORD_TABLE_ENTRIES[key].string;

            if (*str == *s && !memcmp (str + 1, s + 1, len - 1))
              return &KEYWORD_TABLE_ENTRIES[key];
          }
    }
  return static_cast<struct KeywordTableEntry *> (0);
}
#line 99 "./keyword_table.gperf"


static_assert(sizeof(KeywordTableEntry) == alignof(KeywordTableEntry));
static_assert(alignof(KeywordTableEntry) >= GPerfFixedString::STORAGE_SIZE);
static_assert(KEYWORD_TABLE_MIN_WORD_LENGTH >= 2);
static_assert(KEYWORD_TABLE_MAX_WORD_LENGTH <= GPerfFixedString::MAX_KEYWORD_LENGTH);
}
