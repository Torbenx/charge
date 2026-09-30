#pragma once

#include <verify/ir/Database.h>

#include <verify/language/IdentifierTable.h>

namespace verify::language {

struct TokenStream;

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

struct ParsedDatabase {
    std::vector<std::string> functionNames;
    std::vector<std::string> typeImplNames;
    std::vector<std::string> memberNames;
    std::vector<std::string> invariantNames;

    ir::Database database;
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

enum class GlobalKind : uint8_t {
    Function,
    TypeImpl,
    Member,
    Invariant,
};

struct GlobalName {
    GlobalName(GlobalKind kind, uint32_t id)
        : m_kind(std::to_underlying(kind)), m_id(id) { }
    explicit GlobalName(ir::FnHandle fn)
        : GlobalName(GlobalKind::Function, fn.id()) { }
    explicit GlobalName(ir::TypeImpl impl)
        : GlobalName(GlobalKind::TypeImpl, impl.id()) { }
    explicit GlobalName(ir::MemberLiteral member)
        : GlobalName(GlobalKind::Member, member.id()) { }
    explicit GlobalName(ir::Invariant inv)
        : GlobalName(GlobalKind::Invariant, inv.id()) { }

    GlobalKind kind() const { return (GlobalKind)m_kind; }
    uint32_t id() const { return m_id; }

    ir::FnHandle fn() const {
        VERIFY(kind() == GlobalKind::Function);
        return ir::FnHandle(id());
    }
    ir::TypeImpl typeImpl() const {
        VERIFY(kind() == GlobalKind::TypeImpl);
        return ir::TypeImpl(id());
    }
    ir::MemberLiteral member() const {
        VERIFY(kind() == GlobalKind::Member);
        return ir::MemberLiteral(id());
    }
    ir::Invariant invariant() const {
        VERIFY(kind() == GlobalKind::Invariant);
        return ir::Invariant(id());
    }

private:
    uint32_t m_kind : 8 = 0;
    uint32_t m_id : 24 = 0;
};

struct ParseContext {
    IdentifierTable wordTable;
    LookupTable<GlobalName> globals;

    ParsedFunction parseFunction(TokenStream& s);
};

//! Freestanding parse function, parses exactly one function definition
ParsedFunction parseFunction(const char* source);

}