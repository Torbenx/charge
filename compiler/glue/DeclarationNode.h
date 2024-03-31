#pragma once

#include <WordTable.h>
#include <parse/Output.h>
#include <ranges>
#include <utility>

namespace sema {

struct Program;

}

namespace glue {

class DeclarationNode {
public:
    enum class Kind : uint8_t {
        Namespace,
        Type,
        Variable,
        Function,
        Member,
        HasMember,
    };

    DeclarationNode(Kind kind, Word name, DeclarationNode* declaring, parse::TokenHandle parseLocation)
        : m_kind(std::to_underlying(kind))
        , m_name(name)
        , m_declaringDecl(this, declaring)
        , m_parseLocation(parseLocation) { }

    Kind kind() const { return (Kind)m_kind; }
    Word name() const { return m_name; }
    DeclarationNode* declaringNode() {
        return m_declaringDecl.get(this);
    }
    std::optional<parse::TokenHandle> parseLocation() {
        return m_parseLocation;
    }
    std::optional<sema::Program*> program() { return m_program; }
    void setProgram(std::optional<sema::Program*> program) {
        m_program = program;
    }
    std::optional<DeclarationNode*> findChild(Word name) {
        auto result = m_namedChildren.findWord(name);
        if (result.found)
            return getPtr(result);
        return std::nullopt;
    }
    bool addStaticChild(Word name, DeclarationNode* child) {
        return m_namedChildren.insertWord(name, std::bit_cast<uint32_t>(relative_t(this, child)));
    }

    bool addMember(Word name, DeclarationNode* child) {
        m_members.emplace_back(this, child);
        return addStaticChild(name, child);
    }

    void addHasMember(DeclarationNode* child) {
        m_members.emplace_back(this, child);
    }

    using relative_t = relative_pointer<DeclarationNode, DeclarationNode>;

    DeclarationNode* getPtr(WordTable::LookupState state) {
        return std::bit_cast<relative_t>(m_namedChildren.entries[state.bucket].payload).get(this);
    }

    uint32_t m_kind : 3;
    Word m_name;
    relative_t m_declaringDecl;
    parse::TokenHandle m_parseLocation;
    std::optional<sema::Program*> m_program;
    WordTable m_namedChildren;
    std::vector<relative_t> m_members;
};

}