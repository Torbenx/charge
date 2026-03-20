#pragma once

#include <verify/backend/Data.h>

#include <utility>

namespace verify::backend {

struct DataManager {
    struct CommonDataInfo {
        CommonDataInfo(int_t elementSize, int_t groupSize, DataInitializeFunction i, DataDestroyFunction d)
            : elementSize(elementSize), groupSize(groupSize), initFunction(i), destroyFunction(d) { }
        uint32_t elementSize = 0;
        uint32_t groupSize = 0;
        DataInitializeFunction initFunction = nullptr;
        DataDestroyFunction destroyFunction = nullptr;

        size_t requiredBytes(int_t valueCapacity) const {
            return ((size_t)valueCapacity / (size_t)groupSize) * (size_t)elementSize;
        }
        int_t elementCount(int_t valueCount) const {
            return valueCount / (int_t)groupSize;
        }
        Value elementValue(TheoryId theory, int_t elementIdx) const {
            return Value(theory, elementIdx * groupSize);
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
    Value newValue(TheoryId);
    void registerTheoryData(TheoryDataBase&, TheoryId, int_t elementSize, int_t groupSize, DataInitializeFunction, DataDestroyFunction);
    void registerKindData(KindDataBase&, ValueKind, int_t elementSize, int_t groupSize, DataInitializeFunction, DataDestroyFunction);
    ~DataManager();

    std::array<ValueKindInfo, std::to_underlying(ValueKind::COUNT)> kinds;
    std::array<TheoryInfo, std::to_underlying(TheoryId::COUNT)> theories;
};

}