#include <parse/Parser.h>

#include <gtest/gtest.h>

namespace parse {

Parser::Parser(const char* sourcePosition)
    : m_state {
        .sourcePosition = sourcePosition,
        .scopePosition = scopeBuffer.buffer,
        .argumentPosition = argumentBuffer.buffer
    } { }

SimpleParser::SimpleParser()
    : SimpleParser("") { }

SimpleParser::SimpleParser(const char* sourcePosition)
    : m_state {
        .sourcePosition = sourcePosition,
        .scopePosition = scopeBuffer.buffer
    } { }

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
        m_state.continueState,
        m_state.parsedTokens,
        m_state.sourcePosition,
        scopeBuffer.save(m_state.scopePosition)
    };
}

void SimpleParser::restore(const SavedParserState& in) {
    m_state = {
        in.status,
        in.state,
        in.continueState,
        in.parsedTokens,
        in.sourcePosition,
        scopeBuffer.restore(in.scopeBuffer)
    };
}

SavedParserState SimpleParser::saveStateOf(const Parser& parser) {
    return {
        parser.m_state.status,
        parser.m_state.state,
        parser.m_state.continueState,
        parser.m_state.parsedTokens,
        parser.m_state.sourcePosition,
        parser.scopeBuffer.save(parser.m_state.scopePosition),
    };
}

void SimpleParser::copyState(const Parser& parser) {
    restore(saveStateOf(parser));
}

TEST(Parse, LexEOF) {
    std::string_view source = "a\nstatic +";
    SimpleParser parser(source.data());
    EXPECT_EQ(parser.lexToken(), LexerToken::Identifier);
    EXPECT_EQ(parser.lexToken(), LexerToken::Static);
    EXPECT_EQ(parser.lexToken(), LexerToken::Plus);
    EXPECT_EQ(parser.lexToken(), LexerToken::EOS);
}

TEST(Parse, TokenCount) {
    {
        SimpleParser parser("static a = a;");
        SimpleOutput output;
        parser.parse(output);
        EXPECT_EQ(parser.parsedTokens(), 6); // Includes one EOF token
    }
    {
        SimpleParser parser("static a = a;");
        SimpleOutput output;
        parser.parse(output, 2);
        EXPECT_EQ(parser.parsedTokens(), 2);
    }
    {
        SimpleParser parser("static a = ;");
        SimpleOutput output;
        parser.parse(output);
        EXPECT_EQ(parser.parsedTokens(), 3);
    }
}

}