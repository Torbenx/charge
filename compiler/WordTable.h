#pragma once

#include <log.h>
#include <types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include <vector>

struct Word {
    static constexpr int ID_BITS = 4;
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

    uint32_t idBits : ID_BITS = 0;
    uint32_t hashBits : (32 - ID_BITS) = 0;

    constexpr Word() = default;
    constexpr Word(uint8_t id, uint32_t hash)
        : idBits(id), hashBits(hash >> ID_BITS) { }
    static constexpr Word fromUint(uint32_t in) {
        return Word(in & MAX_ID, in & ~MAX_ID);
    }
    constexpr uint32_t hash() const { return hashBits << ID_BITS; }
    constexpr uint8_t id() const { return idBits; }
    void setId(uint8_t id) { idBits = id; }
    constexpr bool keyword() const { return idBits == 0; }
    constexpr bool operator==(const Word& other) const {
        return idBits == other.idBits && hashBits == other.hashBits;
    }
    constexpr bool empty() const { return *this == Word(); }
    constexpr uint32_t toUint() const { return id() | hash(); }
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
    constexpr bool empty() const { return invLogSize == 0; }
    constexpr int_t bucketCount() const { return static_cast<uint32_t>((uint64_t)0x1'0000'0000 >> invLogSize); }
    constexpr int_t entryCount() const { return usedBuckets; }

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
    // TODO: This API is awkward, it can easily lead to misused when the table is empty
    constexpr FindResult findWord(Word) const;
    constexpr FindResult continueFindWord(Word, LookupState) const;

private:
    constexpr WordTableView(Entry* entries, uint32_t size)
        : entries(entries), invLogSize(32 - std::countr_zero(size)) {
        VERIFY(size == 0 || std::has_single_bit(size));
    }
    friend struct WordTable;
};

constexpr WordTableView::FindResult WordTableView::findWord(Word word) const {
    if (empty())
        return { { 0 }, false };

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

struct WordTable : WordTableView {
    constexpr WordTable();
    constexpr WordTable(std::span<Entry> entries, uint32_t numEntries)
        : WordTableView(entries) { usedBuckets = numEntries; }
    constexpr WordTable(const WordTable& other);
    constexpr ~WordTable();

    constexpr void rehash();
    constexpr void maybeRehash();
    constexpr void initializeFromEmpty();

    constexpr bool insertWord(Word word, uint32_t payload);
};

constexpr bool WordTable::insertWord(Word word, uint32_t payload) {
    if (empty())
        initializeFromEmpty();

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
constexpr void WordTable::initializeFromEmpty() {
    VERIFY(empty());
    invLogSize = 30;
    std::allocator<Entry> allocator;
    entries = allocator.allocate(bucketCount());
    std::uninitialized_fill_n(entries, bucketCount(), Entry());
}

constexpr WordTable::WordTable()
    : WordTableView(nullptr, 0) { }

constexpr WordTable::WordTable(const WordTable& other)
    : WordTableView(other) {
    if (!other.empty()) {
        std::allocator<Entry> allocator;
        entries = allocator.allocate(bucketCount());
        std::uninitialized_copy_n(other.entries, bucketCount(), entries);
    }
}

constexpr WordTable::~WordTable() {
    if (entries) {
        std::destroy_n(entries, bucketCount());
        std::allocator<Entry> allocator;
        allocator.deallocate(entries, bucketCount());
    }
}