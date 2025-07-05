#pragma once

#include <WordTable.h>

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
        : WordTable() { initializeFromEmpty(); }
    template<typename... Ts>
    constexpr WordStringTable(const ConstWordStringTable<Ts...>&);
    constexpr WordStringTable(const WordStringTable&);
    constexpr ~WordStringTable();

    constexpr Word get(std::string_view str);
    // hash must match wordHash()
    constexpr Word getWithHash(std::string_view str, uint32_t hash);
    constexpr Word insertKeyword(std::string_view str);

    constexpr std::string_view view(Word word) const;

    template<typename F>
    constexpr void forEachWord(F&& f) const {
        for (int_t bucket = 0; bucket < bucketCount(); bucket++) {
            if (!entries[bucket].empty())
                f(entries[bucket].word, getStorage(entries[bucket].payload));
        }
    }

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
            if (entry.word.id() >= firstValidId) {
                firstValidId = entry.word.id() + 1;
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
    VERIFY(word.id() == 0);
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