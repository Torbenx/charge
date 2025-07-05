#pragma once

#include <WordTable.h>
#include <sema/Constant.h>

namespace sema {

struct Scope {
    WordTable m_table;

    std::optional<DeclarationValue> getDeclaration(Word name) const {
        auto result = m_table.findWord(name);
        if (result.found)
            return DeclarationValue::fromUint(m_table.entries[result.bucket].payload);
        return std::nullopt;
    }

    void addDeclaration(Word name, DeclarationValue value) {
        [[maybe_unused]] bool existedAlready = m_table.insertWord(name, value.toUint());
        VERIFY(!existedAlready);
    }

    template<typename F>
    void forEachDeclration(F&& f) const {
        for (int_t bucket = 0; bucket < m_table.bucketCount(); bucket++) {
            auto entry = m_table.entries[bucket];
            if (!entry.empty())
                f(entry.word, DeclarationValue::fromUint(entry.payload));
        }
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