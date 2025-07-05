#pragma once

#include <WordTable.h>

class WordTranslationTable : private WordTable {
private:
    static constexpr int_t IDS_PER_ENTRY = 32 / Word::ID_BITS;

    static constexpr void setId(uint32_t& payload, uint8_t source, uint8_t target) {
        uint32_t mask = Word::MAX_ID << (source * Word::ID_BITS);
        uint32_t value = (uint32_t)target << (source * Word::ID_BITS);
        payload = (payload & ~mask) | value;
    }

    static constexpr uint8_t getId(uint32_t payload, uint8_t source) {
        return (payload >> (source * Word::ID_BITS)) & Word::MAX_ID;
    }

    static consteval auto identityPayload() {
        static constexpr int_t size = Word::MAX_ID / IDS_PER_ENTRY + 1;
        std::array<uint32_t, size> result;
        result.fill(0);
        for (int_t id = 0; id <= Word::MAX_ID; id++) {
            auto [high, low] = splitSourceId(id);
            setId(result[high], low, id);
        }
        return result;
    }

    static constexpr std::pair<uint8_t, uint8_t> splitSourceId(uint8_t source) {
        return { source / IDS_PER_ENTRY, source % IDS_PER_ENTRY };
    }

public:
    constexpr WordTranslationTable()
        : WordTable() { initializeFromEmpty(); }

    constexpr void insert(Word source, Word target) {
        VERIFY(source.hash() == target.hash());
        if (source.id() == target.id())
            return;

        auto [sourceHigh, sourceLow] = splitSourceId(source.id());
        Word lookup = source;
        lookup.setId(sourceHigh);
        FindResult result = findWord(lookup);
        Entry& entry = entries[result.bucket];
        if (result.found) {
            setId(entry.payload, sourceLow, target.id());
        } else {
            // entry is guaranteed to be currently empty
            entry = { lookup, identityPayload()[sourceHigh] };
            setId(entry.payload, sourceLow, target.id());
            usedBuckets += 1;
            maybeRehash();
        }
    }

    constexpr Word get(Word source) const {
        auto [sourceHigh, sourceLow] = splitSourceId(source.id());
        Word lookup = source;
        lookup.setId(sourceHigh);
        FindResult result = findWord(lookup);
        if (!result.found) {
            return source;
        } else {
            Word target = source;
            target.setId(getId(entries[result.bucket].payload, sourceLow));
            return target;
        }
    }
};