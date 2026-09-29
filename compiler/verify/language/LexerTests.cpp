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

    // An error says which character it is about and where it stands
    try {
        lexFile("fn #test($a):\n    store $a <- $a ⊕ $a\n");
        ADD_FAILURE() << "Expected the unknown operator to be reported";
    } catch (const ParserException& e) {
        EXPECT_EQ(std::string(e.what()), "2:20: Unknown operator '⊕'");
    }
}

TEST(VerifyLanguage, LexColonInNames) {
    // A name may contain colons, but the colons it ends with are read on their own
    LexedFile file = lexFile("fn #S:m($a::b, $c):\n@l:x:\n    iassert #A:invariant $a::b\n");
    std::vector<std::pair<TokenKind, std::string_view>> expected = {
        { TokenKind::BeginScope, {} },
        { TokenKind::Identifier, "fn" },
        { TokenKind::GlobalName, "S:m" },
        { TokenKind::LeftParen, {} },
        { TokenKind::LocalName, "a::b" },
        { TokenKind::Comma, {} },
        { TokenKind::LocalName, "c" },
        { TokenKind::RightParen, {} },
        { TokenKind::Colon, {} },
        { TokenKind::BeginScope, {} },
        { TokenKind::ContinueScope, "l:x" },
        { TokenKind::Identifier, "iassert" },
        { TokenKind::GlobalName, "A:invariant" },
        { TokenKind::LocalName, "a::b" },
        { TokenKind::EndScope, {} },
        { TokenKind::EndScope, {} },
    };
    ASSERT_EQ(file.tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(file.tokens[i].kind(), expected[i].first);
        if (!expected[i].second.empty())
            EXPECT_EQ(file.wordTable.view(file.tokens[i].word()), expected[i].second);
    }

    // A name may begin with colons, as long as more of the name follows them
    LexedFile leading = lexFile("fn #f($a):\n    call #::f($:a)\n");
    EXPECT_EQ(leading.tokens[9].kind(), TokenKind::GlobalName);
    EXPECT_EQ(leading.wordTable.view(leading.tokens[9].word()), "::f");
    EXPECT_EQ(leading.tokens[11].kind(), TokenKind::LocalName);
    EXPECT_EQ(leading.wordTable.view(leading.tokens[11].word()), ":a");
}

static void expectTokens(const LexedFile& file, const std::vector<std::pair<TokenKind, std::string_view>>& expected) {
    ASSERT_EQ(file.tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(file.tokens[i].kind(), expected[i].first) << "at token " << i;
        if (!expected[i].second.empty())
            EXPECT_EQ(file.wordTable.view(file.tokens[i].word()), expected[i].second) << "at token " << i;
    }
}

TEST(VerifyLanguage, LexIndentedLabels) {
    // A label sits at the indentation of the header whose body it labels, wherever that header is
    LexedFile file = lexFile(
        "struct #S:\n"
        "    fn #m($r):\n"
        "    @entry:\n"
        "        nop\n"
        "    @exit:\n"
        "        nop\n"
        "    @l: nop\n"
        "    fn #n():\n"
        "        nop\n");
    expectTokens(file,
        {
            { TokenKind::BeginScope, {} },
            { TokenKind::Identifier, "struct" },
            { TokenKind::GlobalName, "S" },
            { TokenKind::Colon, {} },
            { TokenKind::BeginScope, {} },
            { TokenKind::Identifier, "fn" },
            { TokenKind::GlobalName, "m" },
            { TokenKind::LeftParen, {} },
            { TokenKind::LocalName, "r" },
            { TokenKind::RightParen, {} },
            { TokenKind::Colon, {} },
            { TokenKind::BeginScope, {} },
            { TokenKind::ContinueScope, "entry" },
            { TokenKind::Identifier, "nop" },
            { TokenKind::ContinueScope, "exit" },
            { TokenKind::Identifier, "nop" },
            { TokenKind::ContinueScope, "l" },
            { TokenKind::Identifier, "nop" },
            { TokenKind::EndScope, {} },
            { TokenKind::ContinueScope, {} },
            { TokenKind::Identifier, "fn" },
            { TokenKind::GlobalName, "n" },
            { TokenKind::LeftParen, {} },
            { TokenKind::RightParen, {} },
            { TokenKind::Colon, {} },
            { TokenKind::BeginScope, {} },
            { TokenKind::Identifier, "nop" },
            { TokenKind::EndScope, {} },
            { TokenKind::EndScope, {} },
            { TokenKind::EndScope, {} },
        });
    EXPECT_TRUE(file.tokens[19].word().empty());
}

TEST(VerifyLanguage, LexLabelIndentationErrors) {
    // A label is not indented to the body it labels
    EXPECT_THROW(lexFile("fn #test():\n    @l:\n    nop\n"), ParserException);
    EXPECT_THROW(lexFile("fn #test():\n    nop\n    @l:\n    nop\n"), ParserException);
    // A label of an indented body is neither at the start of the line nor between the scopes
    EXPECT_THROW(lexFile("struct #S:\n    fn #m():\n@l:\n        nop\n"), ParserException);
    EXPECT_THROW(lexFile("struct #S:\n    fn #m():\n  @l:\n        nop\n"), ParserException);
    EXPECT_THROW(lexFile("struct #S:\n    fn #m():\n        nop\n@l:\n        nop\n"), ParserException);
    // There is no body a label in front of a line of the file scope could label
    EXPECT_THROW(lexFile("fn #f():\n    nop\n@l:\nfn #g():\n    nop\n"), ParserException);

    try {
        lexFile("struct #S:\n    fn #m():\n  @entry:\n        nop\n");
        ADD_FAILURE() << "Expected the misplaced label to be reported";
    } catch (const ParserException& e) {
        EXPECT_EQ(std::string(e.what()), "3:3: Label is not indented to the header of its body");
    }
}

TEST(VerifyLanguage, LexContinuationBeginningWithLabel) {
    // A line beginning with a label that no ':' follows continues the expression before it
    LexedFile file = lexFile("fn #test($a):\n    store $a <- @a.active or\n        @b.active\n");
    expectTokens(file,
        {
            { TokenKind::BeginScope, {} },
            { TokenKind::Identifier, "fn" },
            { TokenKind::GlobalName, "test" },
            { TokenKind::LeftParen, {} },
            { TokenKind::LocalName, "a" },
            { TokenKind::RightParen, {} },
            { TokenKind::Colon, {} },
            { TokenKind::BeginScope, {} },
            { TokenKind::Identifier, "store" },
            { TokenKind::LocalName, "a" },
            { TokenKind::LeftArrow, {} },
            { TokenKind::LabelName, "a" },
            { TokenKind::Point, {} },
            { TokenKind::Identifier, "active" },
            { TokenKind::Identifier, "or" },
            { TokenKind::BeginScope, {} },
            { TokenKind::LabelName, "b" },
            { TokenKind::Point, {} },
            { TokenKind::Identifier, "active" },
            { TokenKind::EndScope, {} },
            { TokenKind::EndScope, {} },
            { TokenKind::EndScope, {} },
        });
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
