#pragma once

#include <WordStringTable.h>
#include <verify/ir/Database.h>

#include <utility>

namespace verify::language {

inline constexpr ConstWordStringTable words {
    "fn"
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

    WordTable m_table;
};

enum class LocalKind {
    Parameter,
    LocalMemoryLocation,
    Alias,
};

struct Local {
    Local(LocalKind kind, uint32_t id)
        : kindBits(std::to_underlying(kind)), idBits(id) { }

    LocalKind kind() const { return (LocalKind)kindBits; }
    uint32_t id() const { return idBits; }

    uint32_t kindBits : 8;
    uint32_t idBits : 24;
};

}