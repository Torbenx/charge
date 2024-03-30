#pragma once

#include <log.h>
#include <types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include <vector>

struct Word {
    static constexpr int ID_BITS = 3;
    static constexpr uint32_t MAX_ID = (1u << ID_BITS) - 1;
    struct HashState {
        uint32_t hash = 0;
        uint32_t latent = 0;
        HashState() = default;
    };
    static constexpr void iterateHash(HashState& state, uint8_t c) {
        // state.hash = state.hash + (state.hash >> 5) + (state.hash << 7) + (c << 26) + c;
        uint32_t newHash = state.latent + (state.hash >> 5) + (c << 26);
        state.latent = state.hash + (state.hash << 7) + c;
        state.hash = newHash;
    }
    static constexpr uint32_t finalizeHash(HashState state) {
        return (state.hash + state.latent) & ~((1u << Word::ID_BITS) - 1);
    }
    static constexpr uint32_t hash(std::string_view str) {
        HashState hash;
        for (char c : str)
            iterateHash(hash, c);
        return finalizeHash(hash);
    }

    uint32_t id : ID_BITS = 0;
    uint32_t hashBits : (32 - ID_BITS) = 0;

    constexpr Word() = default;
    constexpr Word(uint8_t id, uint32_t hash)
        : id(id), hashBits(hash >> ID_BITS) { }
    static constexpr Word fromUint(uint32_t in) {
        return Word(in >> (32 - ID_BITS), in & MAX_ID);
    }
    constexpr uint32_t hash() const { return hashBits << ID_BITS; }
    constexpr bool keyword() const { return id == 0; }
    constexpr bool operator==(const Word& other) const {
        return id == other.id && hashBits == other.hashBits;
    }
    constexpr bool empty() const { return *this == Word(); }
    constexpr uint32_t asUint() const { return id | hash(); }
};

struct WordTable;
struct WordTableView {
    struct Ratio {
        int_t nom;
        int_t denom;

        constexpr double ratio() const { return (double)nom / denom; };
    };
    static constexpr Ratio MAX_LOAD_RATIO = { 3, 4 };
    struct Entry {
        Word word;
        uint32_t payload = 0;
        constexpr bool empty() const { return word.empty(); }
    };

    Entry* entries;
    uint32_t invLogSize; // = 32 - log2(size)
    uint32_t usedBuckets = 0;

    constexpr WordTableView(std::span<const Entry> entries)
        : WordTableView(const_cast<Entry*>(entries.data()), entries.size()) { }

    constexpr uint32_t hashToBucket(uint32_t hash) const {
        return (uint64_t)hash >> invLogSize;
    }
    constexpr uint32_t modSize(uint32_t in) const {
        return in & hashToBucket(-1);
    }
    constexpr int_t bucketCount() const { return static_cast<uint32_t>((uint64_t)0x1'0000'0000 >> invLogSize); }

    struct LookupState {
        uint32_t bucket;
    };
    struct FindResult : LookupState {
        bool found = false;
    };
    constexpr LookupState beginLookup(uint32_t hash) const {
        return { .bucket = hashToBucket(hash) };
    }
    constexpr void advanceLookup(LookupState& state) const {
        state.bucket = modSize(state.bucket + 1);
    }
    constexpr FindResult findWord(Word) const;
    constexpr FindResult continueFindWord(Word, LookupState) const;

private:
    constexpr WordTableView(Entry* entries, uint32_t size)
        : entries(entries), invLogSize(32 - std::countr_zero(size)) {
        VERIFY(std::has_single_bit(size));
    }
    friend struct WordTable;
};
struct WordTable : WordTableView {
    constexpr WordTable();
    constexpr WordTable(std::span<Entry> entries, uint32_t numEntries)
        : WordTableView(entries) { usedBuckets = numEntries; }
    constexpr WordTable(const WordTable& other);
    constexpr ~WordTable();

    constexpr void rehash();
    constexpr void maybeRehash();
    constexpr bool insertWord(Word word, uint32_t payload);

    constexpr int_t entryCount() const { return usedBuckets; }
};

constexpr WordTableView::FindResult WordTableView::findWord(Word word) const {
    LookupState state = beginLookup(word.hash());
    for (;;) {
        const Entry& entry = entries[state.bucket];
        if (entry.empty())
            return { state, false };
        if (entry.word == word)
            return { state, true };

        advanceLookup(state);
    }
}
constexpr WordTableView::FindResult WordTableView::continueFindWord(Word word, LookupState state) const {
    for (;;) {
        advanceLookup(state);

        const Entry& entry = entries[state.bucket];
        if (entry.empty())
            return { state, false };
        if (entry.word == word)
            return { state, true };
    }
}

constexpr bool WordTable::insertWord(Word word, uint32_t payload) {
    auto result = findWord(word);
    if (!result.found) {
        entries[result.bucket] = { word, payload };
        usedBuckets += 1;
        maybeRehash();
    }
    return result.found;
}

constexpr void WordTable::maybeRehash() {
    if (usedBuckets * MAX_LOAD_RATIO.denom <= bucketCount() * MAX_LOAD_RATIO.nom)
        return;
    rehash();
}
constexpr void WordTable::rehash() {
    uint32_t oldSize = bucketCount();
    Entry* oldEntries = entries;
    std::allocator<Entry> allocator;
    invLogSize -= 1;
    VERIFY(invLogSize > 0);
    entries = allocator.allocate(bucketCount());
    std::uninitialized_fill_n(entries, bucketCount(), Entry());
    for (uint32_t i = 0; i < oldSize; i++) {
        Entry& oldEntry = oldEntries[i];
        if (oldEntries[i].empty())
            continue;

        LookupState state = beginLookup(oldEntry.word.hash());
        for (;;) {
            Entry& newEntry = entries[state.bucket];
            if (newEntry.empty())
                break;
            advanceLookup(state);
        }
        entries[state.bucket] = oldEntry;
    }
    std::destroy_n(oldEntries, oldSize);
    allocator.deallocate(oldEntries, oldSize);
}

constexpr WordTable::WordTable()
    : WordTableView(nullptr, 4) {
    std::allocator<Entry> allocator;
    entries = allocator.allocate(bucketCount());
    std::uninitialized_fill_n(entries, bucketCount(), Entry());
}

constexpr WordTable::WordTable(const WordTable& other)
    : WordTableView(other) {
    std::allocator<Entry> allocator;
    entries = allocator.allocate(bucketCount());
    std::uninitialized_copy_n(other.entries, bucketCount(), entries);
}

constexpr WordTable::~WordTable() {
    if (entries) {
        std::destroy_n(entries, bucketCount());
        std::allocator<Entry> allocator;
        allocator.deallocate(entries, bucketCount());
    }
}

struct WrappedWordStringTable;
template<typename... Ts>
struct ConstWordStringTable;

class WordStringTable : private WordTable {
private:
    friend struct WrappedWordStringTable;
    template<typename... Ts>
    friend struct ConstWordStringTable;

    constexpr Word insertInternal(std::string_view str, uint32_t hash, size_t firstValidId, size_t firstInvalidId);

    char* stringStorage = nullptr;
    uint32_t stringStorageOffset = 0;
    uint32_t stringStorageCapacity = 0;
    constexpr uint32_t allocateStorage(std::string_view str);
    constexpr std::string_view getStorage(uint32_t index) const;

    constexpr WordStringTable(std::span<Entry> entries, std::span<char> stringStorage, uint32_t numEntries, uint32_t stringStorageOffset);
    constexpr void clearPointers();

public:
    constexpr WordStringTable()
        : WordTable() { }
    template<typename... Ts>
    constexpr WordStringTable(const ConstWordStringTable<Ts...>&);
    constexpr WordStringTable(const WordStringTable&);
    constexpr ~WordStringTable();

    constexpr Word get(std::string_view str);
    // hash must match wordHash()
    constexpr Word getWithHash(std::string_view str, uint32_t hash);
    constexpr Word insertKeyword(std::string_view str);

    constexpr std::string_view view(Word word) const;

    class EntryHandle {
    private:
        uint32_t bucket = 0;
        constexpr EntryHandle(uint32_t bucket)
            : bucket(bucket) { }
        friend class WordStringTable;
    };
    constexpr std::string_view view(EntryHandle handle) const {
        return getStorage(entries[handle.bucket].payload);
    }
    constexpr Word word(EntryHandle handle) const {
        return entries[handle.bucket].word;
    }
    template<typename Callback>
    constexpr void forEachEntry(Callback&& callback) const {
        for (uint32_t i = 0; i < bucketCount(); i++) {
            if (entries[i].empty())
                continue;
            callback(EntryHandle(i));
        }
    }
};

// the table must have at least one free slot
constexpr Word WordStringTable::insertInternal(std::string_view str, uint32_t hash, size_t firstValidId, size_t firstInvalidId) {
    LookupState state = beginLookup(hash);
    for (;;) {
        Entry& entry = entries[state.bucket];
        if (entry.empty()) {
            Word word(firstValidId, hash);
            entry.word = word;
            entry.payload = allocateStorage(str);
            usedBuckets += 1;

            maybeRehash();
            return word;
        }

        if (entry.word.hash() == hash) {
            if (getStorage(entry.payload) == str) [[likely]]
                return entry.word;
            // hash collision, keep track of the highest seen id
            if (entry.word.id >= firstValidId) {
                firstValidId = entry.word.id + 1;
                VERIFY(firstValidId < firstInvalidId);
            }
        }

        advanceLookup(state);
    }
}

constexpr uint32_t WordStringTable::allocateStorage(std::string_view string) {
    uint32_t offset = stringStorageOffset;
    VERIFY(offset % 2 == 0);
    uint32_t requiredCapacity = stringStorageOffset + 2 + string.length();
    if (requiredCapacity > stringStorageCapacity) {
        VERIFY(!std::is_constant_evaluated());
        uint32_t newCapacity = std::bit_ceil(requiredCapacity);
        std::allocator<char> allocator;
        char* newStorage = allocator.allocate(newCapacity);
        if (stringStorage)
            std::copy_n(stringStorage, stringStorageOffset, newStorage);
        allocator.deallocate(stringStorage, stringStorageCapacity);
        stringStorage = newStorage;
        stringStorageCapacity = newCapacity;
    }
    auto lengthBuf = std::bit_cast<std::array<char, 2>>((uint16_t)string.length());
    std::copy(lengthBuf.begin(), lengthBuf.end(), &stringStorage[stringStorageOffset]);
    stringStorageOffset += 2;
    std::copy(string.begin(), string.end(), &stringStorage[stringStorageOffset]);
    stringStorageOffset += string.length();
    if (stringStorageOffset % 2 == 1)
        stringStorageOffset += 1;
    return offset / 2;
}
constexpr std::string_view WordStringTable::getStorage(uint32_t index) const {
    uint32_t offset = index * 2;
    std::array<char, 2> lengthBuffer;
    std::copy_n(&stringStorage[offset], 2, lengthBuffer.data());
    auto length = std::bit_cast<uint16_t>(lengthBuffer);
    return { &stringStorage[offset] + 2, length };
}

constexpr WordStringTable::WordStringTable(const WordStringTable& other)
    : WordTable(other)
    , stringStorageOffset(other.stringStorageOffset)
    , stringStorageCapacity(other.stringStorageCapacity) {
    if (other.stringStorage) {
        std::allocator<char> allocator;
        stringStorage = allocator.allocate(stringStorageCapacity);
        std::copy_n(other.stringStorage, other.stringStorageOffset, stringStorage);
    }
}
template<typename... Ts>
constexpr WordStringTable::WordStringTable(const ConstWordStringTable<Ts...>& constTable)
    : WordStringTable(constTable.get()) { }
constexpr WordStringTable::WordStringTable(std::span<WordTable::Entry> entries, std::span<char> stringStorage, uint32_t numEntries, uint32_t stringStorageOffset)
    : WordTable(entries, numEntries)
    , stringStorage(stringStorage.data())
    , stringStorageOffset(stringStorageOffset)
    , stringStorageCapacity(stringStorage.size()) { }
constexpr WordStringTable::~WordStringTable() {
    if (stringStorage) {
        std::allocator<char> allocator;
        allocator.deallocate(stringStorage, stringStorageCapacity);
    }
}
constexpr void WordStringTable::clearPointers() {
    entries = nullptr;
    stringStorage = nullptr;
}

constexpr Word WordStringTable::get(std::string_view str) {
    return getWithHash(str, Word::hash(str));
}
constexpr Word WordStringTable::getWithHash(std::string_view str, uint32_t hash) {
    if (str.empty())
        return Word();
    return insertInternal(str, hash, 1, Word::MAX_ID + 1);
}
constexpr Word WordStringTable::insertKeyword(std::string_view str) {
    uint32_t hash = Word::hash(str);
    VERIFY(hash != 0);
    Word word = insertInternal(str, hash, 0, 1);
    VERIFY(word.id == 0);
    return word;
}
constexpr std::string_view WordStringTable::view(Word word) const {
    if (word.empty())
        return {};
    auto result = findWord(word);
    VERIFY(result.found);
    return getStorage(entries[result.bucket].payload);
}

struct WrappedWordStringTable : WordStringTable {
    using WordStringTable::WordStringTable;
    constexpr ~WrappedWordStringTable() { clearPointers(); }
};
template<int_t N>
struct ConstWordStringTableKeyword {
    char buffer[N] = {};
    constexpr ConstWordStringTableKeyword(const char* s) { std::copy_n(s, N, buffer); }
};
template<int_t N>
constexpr auto keyword(const char (&s)[N]) { return ConstWordStringTableKeyword<N>(s); }
template<typename T>
struct ConstWordStringTableString;
template<int_t N>
struct ConstWordStringTableString<char[N]> {
    static constexpr int_t LENGTH = N - 1;
    static constexpr void insert(WordStringTable& table, const char (&s)[N]) {
        table.get(std::string_view(s, LENGTH));
    }
};
template<int_t N>
struct ConstWordStringTableString<ConstWordStringTableKeyword<N>> {
    static constexpr int_t LENGTH = N - 1;
    static constexpr void insert(WordStringTable& table, const ConstWordStringTableKeyword<N>& s) {
        table.insertKeyword(std::string_view(s.buffer, LENGTH));
    }
};
template<typename... Ts>
struct ConstWordStringTable {
private:
    std::array<char, (alignmentCeil(ConstWordStringTableString<Ts>::LENGTH, 2) + ...) + 2 * sizeof...(Ts)> stringStorage = {};
    std::array<WordTable::Entry, std::bit_ceil(static_cast<size_t>(sizeof...(Ts) / WordTable::MAX_LOAD_RATIO.ratio() + 0.5))> entryStorage = {};

    constexpr WrappedWordStringTable get() const {
        return WrappedWordStringTable(const_cast<decltype(entryStorage)&>(entryStorage),
            const_cast<decltype(stringStorage)&>(stringStorage), sizeof...(Ts), stringStorage.size());
    }
    friend class WordStringTable;

public:
    constexpr ConstWordStringTable(const Ts&... strs) {
        WordStringTable table(entryStorage, stringStorage, 0, 0);
        (ConstWordStringTableString<Ts>::insert(table, strs), ...);
        VERIFY(table.stringStorageOffset == stringStorage.size());
        VERIFY(table.entryCount() == (int_t)sizeof...(Ts));
        table.clearPointers();
    }
    consteval Word operator[](std::string_view str) const {
        WrappedWordStringTable table = get();
        Word word = table.get(str);
        return word;
    }
};