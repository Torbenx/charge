#pragma once

#include <verify/ir/Database.h>

#include <verify/language/IdentifierTable.h>
#include <verify/language/Lexer.h>

#include <span>

namespace verify::language {

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

    std::optional<T> tryInsert(Word name, T value) {
        if (m_table.empty())
            m_table.initializeFromEmpty();

        auto result = m_table.findWord(name);
        auto& entry = m_table.entries[result.bucket];
        if (result.found)
            return std::bit_cast<T>(entry.payload);

        entry = { name, std::bit_cast<uint32_t>(value) };
        m_table.usedBuckets += 1;
        m_table.maybeRehash();
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

//! The names a function was written with, and where it was defined
/*!
A name that the source did not spell out is empty, every table has an entry for each of the things it names.
*/
struct FunctionInfo {
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
    //! The member or invariant the function was defined inline in
    /*!
    The function is the type function of the member or the function of the invariant. It is named
    after it and can't be referred to by its name.
    */
    std::optional<GlobalName> inlineOwner;
};

//! A function parsed from the text form together with the names it was written with
struct ParsedFunction : FunctionInfo {
    ir::Function function;
};

struct ParsedDatabase {
    //! Every function, indexed by its handle
    std::vector<FunctionInfo> functions;
    std::vector<std::string> typeImplNames;
    std::vector<std::string> memberNames;
    std::vector<std::string> invariantNames;

    ir::Database database;
};

//! What 'scanFile' learned about a file
struct ScannedFile {
    //! A function whose header was read by 'scanFile', its parameters and body are yet to be parsed
    struct Function {
        ir::FnHandle fn;
        //! At the '(' of the parameter list
        TokenPosition parameters;
        //! At the '(' of the parameter list of the type the function was defined inline in
        /*!
        The parameters of the type come before the ones the function was written with.
        */
        std::optional<TokenPosition> typeParameters;
    };

    //! A member or invariant whose function is named after '=', possibly before it is defined
    struct Alias {
        //! The member or invariant
        GlobalName target;
        //! The name of the function, as if the type name the source may have left out was written
        Word function;
    };

    std::vector<Function> functions;
    std::vector<Alias> aliases;
};

struct ParseContext {
    IdentifierTable wordTable;
    LookupTable<GlobalName> globals;
    ParsedDatabase database;

    //! First phase of parsing a file: Adds everything the file defines to the database
    /*!
    Types are complete afterwards, functions are only reserved.
    */
    ScannedFile scanFile(std::span<const Token> tokens);

    //! Second phase of parsing a file: Parses the functions, now that every global is known
    void parseScannedFile(const ScannedFile& file);

private:
    ParsedFunction parseScannedFunction(const ScannedFile::Function& scanned);
};

//! Freestanding parse function, parses exactly one function definition
ParsedFunction parseFunction(const char* source);

//! Freestanding parse function, parses a whole file
ParsedDatabase parseFile(const char* source);

}