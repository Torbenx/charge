#pragma once

#include <parse/Output.h>
#include <glue/DeclarationNode.h>

namespace glue {

struct Context {
    parse::Output parseOutput;
    WordStringTable wordTable { parse::words };
    DeclarationNode* m_currentNode = nullptr;

    Context(std::string_view source)
        : parseOutput(source) {
        m_currentNode = allocate<DeclarationNode>();
        std::construct_at(m_currentNode, DeclarationNode::Kind::Namespace, Word(), nullptr, parse::TokenHandle());
    }

    DeclarationNode* currentScope() { return m_currentNode; }
    void popScope() {
        m_currentNode = m_currentNode->declaringNode();
    }
    bool pushStaticScope(DeclarationNode::Kind kind, Word name, parse::TokenHandle parseLocation) {
        auto node = m_currentNode->findChild(name);
        if (node.has_value()) {
            m_currentNode = node.value();
            return true;
        }
        DeclarationNode* newNode = allocate<DeclarationNode>();
        std::construct_at(newNode, kind, name, m_currentNode, parseLocation);
        m_currentNode->addStaticChild(name, newNode);
        m_currentNode = newNode;
        return false;
    }
    void pushHasScope(parse::TokenHandle parseLocation) {
        DeclarationNode* newNode = allocate<DeclarationNode>();
        std::construct_at(newNode, DeclarationNode::Kind::HasMember, Word(), m_currentNode, parseLocation);
        m_currentNode->addHasMember(newNode);
        m_currentNode = newNode;
    }
    bool pushMemberScope(Word name, parse::TokenHandle parseLocation) {
        auto node = m_currentNode->findChild(name);
        if (node.has_value()) {
            return true;
        }
        DeclarationNode* newNode = allocate<DeclarationNode>();
        std::construct_at(newNode, DeclarationNode::Kind::Member, Word(), m_currentNode, parseLocation);
        m_currentNode->addMember(name, newNode);
        m_currentNode = newNode;
        return false;
    }

    template<typename T>
    T* allocate() {
        std::allocator<T> allocator;
        return allocator.allocate(1);
    }
};

}