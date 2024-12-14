#pragma once

#include <WordTable.h>
#include <sema/Value.h>

namespace sema {

struct Scope {
    WordTable m_table;

    std::optional<ScopeConstant> getDeclaration(Word name) const {
        auto result = m_table.findWord(name);
        if (result.found)
            return ScopeConstant::fromUint(m_table.entries[result.bucket].payload);
        return std::nullopt;
    }

    void addDeclaration(Word name, ScopeConstant value) {
        [[maybe_unused]] bool existedAlready = m_table.insertWord(name, value.toUint());
        VERIFY(!existedAlready);
    }
};

struct Namespace : Scope {
    Word name;
    std::optional<NamespaceHandle> parent;

    Namespace(Word name, std::optional<NamespaceHandle> parent)
        : name(name), parent(parent) { }
};
static_assert(sizeof(Namespace) == 24);

}