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
        std::construct_at(m_currentNode, DeclarationNode::Kind::Namespace, Word(), nullptr);
    }

    DeclarationNode* currentScope() { return m_currentNode; }
    void popScope() {
        m_currentNode = m_currentNode->declaringNode();
    }
    bool pushNamedScope(DeclarationNode::Kind kind, Word name) {
        auto node = m_currentNode->findChild(name);
        if (node.has_value()) {
            m_currentNode = node.value();
            return true;
        }
        DeclarationNode* newNode = allocate<DeclarationNode>();
        std::construct_at(newNode, kind, name, m_currentNode);
        m_currentNode->addNamedChild(name, newNode);
        m_currentNode = newNode;
        return false;
    }
    void pushUnnamedScope(DeclarationNode::Kind kind) {
        DeclarationNode* newNode = allocate<DeclarationNode>();
        std::construct_at(newNode, kind, Word(), m_currentNode);
        m_currentNode->addUnnamedChild(newNode);
        m_currentNode = newNode;
    }

    template<typename T>
    T* allocate() {
        std::allocator<T> allocator;
        return allocator.allocate(1);
    }
};

}