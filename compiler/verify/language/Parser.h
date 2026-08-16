#pragma once

#include <WordStringTable.h>
#include <verify/ir/Database.h>

#include <utility>

namespace verify::language {

inline constexpr ConstWordStringTable words {
    "active",
    "from",
    "store",
    "call",
    "jump",
    "branch",
    "phi",
    "nop",
    "true",
    "false",
    "and",
    "or",
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
    "phi_active_source",
    "phi_load",
    "branch_decision",
#define SORT(name, snake_case) #snake_case, #snake_case "_scalar",
#include <verify/ir/sorts.inc>
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

    void forEachEntry(auto&& callback) const {
        for (int_t bucket = 0; bucket < m_table.bucketCount(); bucket++) {
            const auto& entry = m_table.entries[bucket];
            if (!entry.empty())
                callback(entry.word, std::bit_cast<T>(entry.payload));
        }
    }

    WordTable m_table;
};

//! A function parsed from the text form together with the names it was written with
/*!
A 'Word' is only meaningful together with the table it was interned in, so the names of the
placeholders a caller wants to address are handed out as strings.
*/
struct ParsedFunction {
    ir::Function function;
    //! The name of every parameter, indexed by its parameter id
    std::vector<std::string> parameterNames;
    //! Every label in the order it was defined
    std::vector<std::pair<std::string, ir::CodePos>> labels;
};

ParsedFunction parse(const char* source);

}