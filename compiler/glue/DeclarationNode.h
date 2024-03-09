#pragma once

#include "WordTable.h"
#include <ranges>
#include <utility>

namespace sema {

struct Program;

}

namespace parse {

struct NodeHandle {
    uint32_t offset;
};

}

namespace glue {

class DeclarationNode {
public:
    enum class Kind : uint8_t {
        Namespace,
        Type,
        Variable,
        Function,
    };

    DeclarationNode(Kind kind, Word name, DeclarationNode* declaring)
        : m_kind(std::to_underlying(kind))
        , m_name(name)
        , m_declaringDecl(declaring) { }

    Kind kind() const { return (Kind)m_kind; }
    Word name() const { return m_name; }
    DeclarationNode* declaringNode() {
        return m_declaringDecl;
    }
    std::optional<parse::NodeHandle> parseLocation() {
        return m_parseLocation;
    }
    std::optional<sema::Program*> program() { return m_program; }
    std::optional<DeclarationNode*> findChild(Word name) {
        auto result = m_namedChildren.findWord(name);
        if (result.found)
            return getPtr(result);
        return std::nullopt;
    }
    bool addNamedChild(Word name, DeclarationNode* child) {
        return m_namedChildren.insertWord(name, std::bit_cast<uint32_t>(relative_t(this, child)));
    }

    void addUnnamedChild(DeclarationNode* child) {
        m_unnamedChildren.emplace_back(this, child);
    }

    using relative_t = relative_pointer<DeclarationNode, DeclarationNode>;

    DeclarationNode* getPtr(WordTable::LookupState state) {
        return std::bit_cast<relative_t>(m_namedChildren.entries[state.bucket].payload).get(this);
    }

    uint32_t m_kind : 2;
    Word m_name;
    DeclarationNode* m_declaringDecl;
    parse::NodeHandle m_parseLocation;
    sema::Program* m_program;
    WordTable m_namedChildren;
    std::vector<relative_t> m_unnamedChildren;
};

}