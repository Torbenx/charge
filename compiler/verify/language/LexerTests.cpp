#include <verify/language/Lexer.h>

#include <gtest/gtest.h>

#include <format>

namespace verify::language {

TEST(VerifyLanguage, LexErrors) {
    // A character outside of ascii is read as a symbol, and has to spell an operator
    EXPECT_THROW(lexFile("fn #test($a):\n    store $a <- $a ⊕ $a\n"), ParserException);
    // The source is required to be encoded as utf8
    EXPECT_THROW(lexFile("fn #test($a):\n    store $a <- $a \xE2\x88 $a\n"), ParserException);
    EXPECT_THROW(lexFile("fn #test($a):\n    store $a <- \x80$a\n"), ParserException);
    // A character cut short by the end of the source is not read past it
    EXPECT_THROW(lexFile("fn #test($a):\n    store $a <- $a \xE2"), ParserException);

    EXPECT_THROW(lexFile("fn #test($a):\n    store $a <- 1\n"), ParserException);
    EXPECT_THROW(lexFile("fn #test($a):\n    store $a < $a\n"), ParserException);
    EXPECT_THROW(lexFile("fn #test($a):\n    nop\n  nop\n"), ParserException);
    EXPECT_THROW(lexFile("fn #test($a):\n@label\n    nop\n"), ParserException);

    // An error says which character it is about and where it stands
    try {
        lexFile("fn #test($a):\n    store $a <- $a ⊕ $a\n");
        ADD_FAILURE() << "Expected the unknown operator to be reported";
    } catch (const ParserException& e) {
        EXPECT_EQ(std::string(e.what()), "2:20: Unknown operator '⊕'");
    }
}

TEST(VerifyLanguage, LexByteOrderMark) {
    // A file may begin with a byte order mark, which says nothing a utf8 source does not
    const char* source = R"(fn #test($a, $b):
    store $a <- $a ≠ $b
)";
    LexedFile plain = lexFile(source);
    LexedFile marked = lexFile(std::format("\xEF\xBB\xBF{}", source).c_str());

    ASSERT_EQ(marked.tokens.size(), plain.tokens.size());
    for (size_t i = 0; i < plain.tokens.size(); i++) {
        EXPECT_EQ(marked.tokens[i].kind(), plain.tokens[i].kind());
        EXPECT_EQ(marked.tokens[i].m_data, plain.tokens[i].m_data);
    }
}

}
