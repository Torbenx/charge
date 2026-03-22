#pragma once

#include <verify/backend/Data.h>

#include <utility>

namespace verify::backend {

struct DataManager {
    struct CommonDataInfo {
        CommonDataInfo(int_t elementSize, int_t groupSize, DataInitializeFunction i, DataDestroyFunction d)
            : elementSize(elementSize), initFunction(i), destroyFunction(d) {
            VERIFY(std::has_single_bit<size_t>(groupSize));
            groupSizeLog2 = std::bit_width<size_t>(groupSize - 1);
        }
        uint32_t elementSize = 0;
        uint32_t groupSizeLog2 = 0;
        DataInitializeFunction initFunction = nullptr;
        DataDestroyFunction destroyFunction = nullptr;

        size_t requiredBytes(int_t valueCapacity) const {
            return ((size_t)valueCapacity >> groupSizeLog2) * (size_t)elementSize;
        }
        int_t elementCount(int_t valueCount) const {
            return valueCount >> groupSizeLog2;
        }
        Value elementValue(TheoryId theory, int_t elementIdx) const {
            return Value(theory, elementIdx << groupSizeLog2);
        }
        int_t elementFor(uint32_t valueId) const {
            return valueId >> groupSizeLog2;
        }
        bool needsNewValue(uint32_t valueId) const {
            return (((1u << groupSizeLog2) - 1u) & valueId) == 0;
        }
    };

    struct TheoryDataInfo : CommonDataInfo {
        TheoryDataBase* base = nullptr;
        std::byte* pointer = nullptr;
    };

    struct KindDataInfo : CommonDataInfo {
        KindDataBase* base = nullptr;
        std::byte** table = nullptr;
    };

    struct TheoryInfo {
        uint32_t valueCount = 0;
        uint32_t dataCapacity = 0;
        std::vector<TheoryDataInfo> datas = {};
    };

    struct ValueKindInfo {
        std::vector<KindDataInfo> datas = {};
    };

    ValueKindInfo& at(ValueKind kind) {
        VERIFY(kind < ValueKind::COUNT);
        return kinds[std::to_underlying(kind)];
    }

    TheoryInfo& at(TheoryId id) {
        VERIFY(id < TheoryId::COUNT);
        return theories[std::to_underlying(id)];
    }

    DataManager();
    Value newValue(TheoryId, int_t count);
    void registerTheoryData(TheoryDataBase&, TheoryId, int_t elementSize, int_t groupSize, DataInitializeFunction, DataDestroyFunction);
    void registerKindData(KindDataBase&, ValueKind, int_t elementSize, int_t groupSize, DataInitializeFunction, DataDestroyFunction);
    ~DataManager();

    std::array<ValueKindInfo, std::to_underlying(ValueKind::COUNT)> kinds;
    std::array<TheoryInfo, std::to_underlying(TheoryId::COUNT)> theories;
};

}