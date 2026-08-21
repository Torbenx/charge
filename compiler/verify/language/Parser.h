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
#define TACTIC(name, snake_case) #snake_case,
#include <verify/ir/tactics.inc>
#define SORT(name, snake_case) #snake_case, #snake_case "_scalar",
#include <verify/ir/sorts.inc>
};

//! The mathematical symbols an operator may be written with
/*!
The source may spell an operator either way, the formatter always writes the symbol. A symbol
never appears inside a name, so reading one is a token of its own.
*/
namespace symbols {
    inline constexpr std::string_view AND = "\u2227"; // ∧
    inline constexpr std::string_view OR = "\u2228"; // ∨
    inline constexpr std::string_view NOT = "\u00ac"; // ¬
    inline constexpr std::string_view NOT_EQUAL = "\u2260"; // ≠
}

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
A name that the source did not spell out is empty, every table has an entry for each of the things it names.
*/
struct ParsedFunction {
    ir::Function function;
    //! The name of the function
    std::string name;
    //! The name of every parameter, indexed by its parameter id
    std::vector<std::string> parameterNames;
    //! The label of every code position, indexed by it
    /*!
    A position may be labeled more than once, only the first of the names is kept.
    It also contains the past-the-end label (if any).
    */
    std::vector<std::string> labels;
    //! The name of every theorem, indexed by its id
    std::vector<std::string> theoremNames;
};

ParsedFunction parse(const char* source);

}