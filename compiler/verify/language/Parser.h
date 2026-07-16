#pragma once

#include <WordStringTable.h>
#include <verify/ir/Database.h>

#include <utility>

namespace verify::language {

inline constexpr ConstWordStringTable words {
    "fn",
    "active",
    "from",
    "store",
    "call",
    "jump",
    "branch",
    "phi",
    "true",
    "false",
    "load",
    "pre",
    "post",
    "prove",
    "clause",
    "by",
    "sat",
    "sorry",
    "eq_reflexive",
    "eq_transitive",
    "load_store",
    "skip_store",
    "phi_enumerate",
    "phi_exclusivity",
    "phi_activate",
    "phi_active_backward",
    "jump_active_forward",
    "branch_active_forward",
    "branch_decision",
};

struct ParserException : std::exception {
    ParserException(std::string message)
        : message(std::move(message)) { }
    std::string message;

    const char* what() const noexcept override {
        return message.data();
    }
};

template<typename T>
struct LookupTable {
    void insert(Word name, T value) {
        m_table.insertWord(name, std::bit_cast<uint32_t>(value));
    }

    std::optional<T> get(Word name) const {
        auto result = m_table.findWord(name);
        if (result.found)
            return std::bit_cast<T>(m_table.entries[result.bucket].payload);
        return std::nullopt;
    }

    std::optional<T> insertOrUpdate(Word name, T value) {
        if (m_table.empty())
            m_table.initializeFromEmpty();

        auto result = m_table.findWord(name);
        auto& entry = m_table.entries[result.bucket];
        if (!result.found) {
            entry = { name, std::bit_cast<uint32_t>(value) };
            m_table.usedBuckets += 1;
            m_table.maybeRehash();
            return std::nullopt;
        } else {
            T ret = std::bit_cast<T>(entry.payload);
            entry.payload = std::bit_cast<uint32_t>(value);
            return ret;
        }
    }

    WordTable m_table;
};

}