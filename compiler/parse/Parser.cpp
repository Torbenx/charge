#include <parse/Parser.h>

#include <gtest/gtest.h>

namespace parse {

Parser::Parser(const char* sourcePosition)
    : m_state {
        .sourcePosition = sourcePosition,
        .scopePosition = scopeBuffer.buffer,
        .argumentPosition = argumentBuffer.buffer
    } { scopeBuffer.buffer[0] = ScopeKind::Invalid; }

SimpleParser::SimpleParser()
    : SimpleParser("") { }

SimpleParser::SimpleParser(const char* sourcePosition)
    : m_state {
        .sourcePosition = sourcePosition,
        .scopePosition = scopeBuffer.buffer
    } { scopeBuffer.buffer[0] = ScopeKind::Invalid; }

void SimpleParser::pushScope(ScopeKind scope) {
    auto index = ScopeBuffer::toIndex(m_state.scopePosition);
    VERIFY(index + 1 < (size_t)SCOPE_BUFFER_SIZE);
    m_state.scopePosition += 1;
    m_state.scopePosition[0] = scope;
}

ScopeKind SimpleParser::popScope() {
    auto index = ScopeBuffer::toIndex(m_state.scopePosition);
    VERIFY(index > 0);
    ScopeKind ret = m_state.scopePosition[0];
    m_state.scopePosition -= 1;
    return ret;
}

SavedParserState SimpleParser::save() const {
    return {
        m_state.status,
        m_state.state,
        m_state.parsedTokens,
        m_state.sourcePosition,
        scopeBuffer.save(m_state.scopePosition)
    };
}

void SimpleParser::restore(const SavedParserState& in) {
    m_state = {
        in.status,
        in.state,
        in.parsedTokens,
        in.sourcePosition,
        scopeBuffer.restore(in.scopeBuffer)
    };
    VERIFY(!error()); // Cannot restore error states
}

SavedParserState SimpleParser::saveStateOf(const Parser& parser) {
    return {
        parser.m_state.status,
        parser.m_state.state,
        parser.m_state.parsedTokens,
        parser.m_state.sourcePosition,
        parser.scopeBuffer.save(parser.m_state.scopePosition),
    };
}

void SimpleParser::copyState(const Parser& parser) {
    restore(saveStateOf(parser));
}

TEST(Parse, LexEOF) {
    const char* source = "a\nstatic +";
    EXPECT_EQ(lexToken(source), LexerToken::Identifier);
    EXPECT_EQ(lexToken(source), LexerToken::Static);
    EXPECT_EQ(lexToken(source), LexerToken::Plus);
    EXPECT_EQ(lexToken(source), LexerToken::EOS);
}

TEST(Parse, StringLiteral) {
    auto parse = [](const char* source) {
        SimpleParser parser(source);
        SimpleOutput output(source);
        parser.parse(output);
        return parser;
    };

    EXPECT_TRUE(parse("static a = \"\";").done());
    EXPECT_TRUE(parse("static a = \"abc\";").done());
    EXPECT_TRUE(parse("static a = \"an escaped quote \\\" does not end it\";").done());
    EXPECT_TRUE(parse("static a = \"an escaped backslash does \\\\\";").done());

    // Anything outside of the printable ascii range ends the literal and is an error
    EXPECT_TRUE(parse("static a = \"unterminated\n;").error());
    EXPECT_TRUE(parse("static a = \"unterminated").error());
    EXPECT_TRUE(parse("static a = \"a\tb\";").error());
    EXPECT_TRUE(parse("static a = \"a\x80z\";").error());
    // The backslash cannot escape a character that may not appear in the literal
    EXPECT_TRUE(parse("static a = \"trailing backslash \\\n\";").error());
}

TEST(Parse, TokenCount) {
    {
        SimpleParser parser("static a = a;");
        SimpleOutput output(parser.sourcePosition());
        parser.parse(output);
        EXPECT_EQ(parser.parsedTokens(), 6); // Includes one EOF token
    }
    {
        SimpleParser parser("static a = a;");
        SimpleOutput output(parser.sourcePosition());
        parser.parse(output, 2);
        EXPECT_EQ(parser.parsedTokens(), 2);
    }
    {
        SimpleParser parser("static a = ;");
        SimpleOutput output(parser.sourcePosition());
        parser.parse(output);
        EXPECT_EQ(parser.parsedTokens(), 3);
    }
}

}