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
#include <parse/parse_gen.h>
#include <cstring>
namespace parse {
#line 24 "./keyword_table.gperf"
struct KeywordTableEntry { const char* string; LexerToken token; };
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
#line 54 "./keyword_table.gperf"
    {"fn",LexerToken::Fn},
#line 36 "./keyword_table.gperf"
    {"else",LexerToken::Else},
#line 35 "./keyword_table.gperf"
    {"elif",LexerToken::Elif},
#line 40 "./keyword_table.gperf"
    {"in",LexerToken::In},
#line 38 "./keyword_table.gperf"
    {"if",LexerToken::If},
#line 50 "./keyword_table.gperf"
    {"while",LexerToken::While},
#line 56 "./keyword_table.gperf"
    {"namespace",LexerToken::Namespace},
#line 41 "./keyword_table.gperf"
    {"let",LexerToken::Let},
#line 39 "./keyword_table.gperf"
    {"impl",LexerToken::Impl},
#line 59 "./keyword_table.gperf"
    {"template",LexerToken::Template},
#line 60 "./keyword_table.gperf"
    {"trait",LexerToken::Trait},
#line 55 "./keyword_table.gperf"
    {"incomplete",LexerToken::Incomplete},
#line 31 "./keyword_table.gperf"
    {"continue",LexerToken::Continue},
#line 30 "./keyword_table.gperf"
    {"const",LexerToken::Const},
#line 58 "./keyword_table.gperf"
    {"struct",LexerToken::Struct},
#line 52 "./keyword_table.gperf"
    {"context",LexerToken::Context},
#line 47 "./keyword_table.gperf"
    {"try",LexerToken::Try},
#line 46 "./keyword_table.gperf"
    {"static",LexerToken::Static},
#line 45 "./keyword_table.gperf"
    {"shared",LexerToken::Shared},
#line 37 "./keyword_table.gperf"
    {"for",LexerToken::For},
#line 33 "./keyword_table.gperf"
    {"discard",LexerToken::Discard},
#line 44 "./keyword_table.gperf"
    {"return",LexerToken::Return},
#line 61 "./keyword_table.gperf"
    {"virtual",LexerToken::Virtual},
#line 43 "./keyword_table.gperf"
    {"prove",LexerToken::Prove},
#line 32 "./keyword_table.gperf"
    {"destroy",LexerToken::Destroy},
#line 42 "./keyword_table.gperf"
    {"loop",LexerToken::Loop},
#line 57 "./keyword_table.gperf"
    {"open",LexerToken::Open},
#line 48 "./keyword_table.gperf"
    {"unique",LexerToken::Unique},
#line 53 "./keyword_table.gperf"
    {"enum",LexerToken::Enum},
#line 51 "./keyword_table.gperf"
    {"base",LexerToken::Base},
#line 28 "./keyword_table.gperf"
    {"break",LexerToken::Break},
#line 29 "./keyword_table.gperf"
    {"catch",LexerToken::Catch},
#line 34 "./keyword_table.gperf"
    {"do",LexerToken::Do},
#line 49 "./keyword_table.gperf"
    {"var",LexerToken::Var},
#line 27 "./keyword_table.gperf"
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
#line 62 "./keyword_table.gperf"


}
