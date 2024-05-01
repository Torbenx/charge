#pragma once

#include <glue/DeclarationNode.h>
#include <parse/Output.h>
#include <sema/Program.h>

namespace glue {

struct Context {
    parse::Output parseOutput;
    WordStringTable wordTable { parse::words };
    PageBumpAllocator<DeclarationNode> storage;
    PageBumpAllocator<sema::ProgramUnion> programs;
    PageBumpAllocator<sema::ProgramHandle> identityTranslation;
    DeclarationNode* m_currentNode = nullptr;

    Context(std::string_view source)
        : parseOutput(source) {
        reset();
    }

    void reset() {
        storage.clear();
        m_currentNode = allocateNode();
        std::construct_at(m_currentNode, DeclarationNode::Kind::Namespace, Word(), nullptr, std::nullopt);
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
        DeclarationNode* newNode = allocateNode();
        std::construct_at(newNode, kind, name, m_currentNode, parseLocation);
        m_currentNode->addStaticChild(name, newNode);
        m_currentNode = newNode;
        return false;
    }
    void pushHasScope(parse::TokenHandle parseLocation) {
        DeclarationNode* newNode = allocateNode();
        std::construct_at(newNode, DeclarationNode::Kind::HasMember, Word(), m_currentNode, parseLocation);
        m_currentNode->addHasMember(newNode);
        m_currentNode = newNode;
    }
    bool pushMemberScope(Word name, parse::TokenHandle parseLocation) {
        auto node = m_currentNode->findChild(name);
        if (node.has_value()) {
            return true;
        }
        DeclarationNode* newNode = allocateNode();
        std::construct_at(newNode, DeclarationNode::Kind::Member, Word(), m_currentNode, parseLocation);
        m_currentNode->addMember(name, newNode);
        m_currentNode = newNode;
        return false;
    }

    DeclarationNode* allocateNode() {
        return storage.allocate();
    }

    sema::ProgramHandle newProgram(sema::ProgramKind kind) {
        sema::ProgramHandle result = { (uint32_t)programs.size() };
        auto* prog = programs.allocate();
        *identityTranslation.allocate() = result;
        std::construct_at(prog, kind);
        prog->get().programTranslationBuffer = identityTranslation.data();
        return result;
    }

    sema::Program* program(sema::ProgramHandle handle) {
        return &programs[handle.id()].get();
    }
};

}