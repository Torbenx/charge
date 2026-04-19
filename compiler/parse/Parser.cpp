#include <parse/Parser.h>

namespace parse {

Parser::Parser(const char* sourcePosition)
    : m_state {
        ReturnStatus::Ready,
        State::NamespaceDeclaration,
        State::NamespaceDeclaration,
        TokenHandle(),
        Word(),
        sourcePosition,
        scopeBuffer.buffer,
        argumentBuffer.buffer
    } {
    m_state.scopePosition[0] = ScopeKind::Invalid;
    m_state.scopePosition += 1;
    m_state.scopePosition[0] = ScopeKind::Namespace;
}

bool Parser::checkFinalState() const {
    if (status() != ReturnStatus::EOS)
        return false;
    VERIFY(scopeBuffer.buffer[0] == ScopeKind::Invalid);
    VERIFY(scopeBuffer.buffer[1] == ScopeKind::Namespace);
    VERIFY(m_state.argumentPosition == argumentBuffer.buffer);
    return m_state.scopePosition == scopeBuffer.buffer + 1;
}

SimpleParser::SimpleParser()
    : m_state {
        ReturnStatus::EOS,
        State::NamespaceDeclaration,
        State::NamespaceDeclaration,
        "",
        scopeBuffer.buffer
    } {
    m_state.scopePosition[0] = ScopeKind::Invalid;
    m_state.scopePosition += 1;
    m_state.scopePosition[0] = ScopeKind::Namespace;
}

SimpleParser::SimpleParser(const char* sourcePosition)
    : m_state {
        ReturnStatus::Ready,
        State::NamespaceDeclaration,
        State::NamespaceDeclaration,
        sourcePosition,
        scopeBuffer.buffer
    } {
    m_state.scopePosition[0] = ScopeKind::Invalid;
    m_state.scopePosition += 1;
    m_state.scopePosition[0] = ScopeKind::Namespace;
}

bool SimpleParser::checkFinalState() const {
    if (status() != ReturnStatus::EOS)
        return false;
    VERIFY(scopeBuffer.buffer[0] == ScopeKind::Invalid);
    VERIFY(scopeBuffer.buffer[1] == ScopeKind::Namespace);
    return m_state.scopePosition == scopeBuffer.buffer + 1;
}

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

SimpleParser::SavedState SimpleParser::save() const {
    return {
        m_state.status,
        m_state.state,
        m_state.continueState,
        m_state.sourcePosition,
        scopeBuffer.save(m_state.scopePosition)
    };
}

void SimpleParser::restore(const SavedState& in) {
    m_state = {
        in.status,
        in.state,
        in.continueState,
        in.sourcePosition,
        scopeBuffer.restore(in.scopeBuffer)
    };
}
void SimpleParser::copyState(const Parser& parser) {
    restore({
        parser.m_state.status,
        parser.m_state.state,
        parser.m_state.continueState,
        parser.m_state.sourcePosition,
        parser.scopeBuffer.save(parser.m_state.scopePosition),
    });
}

}