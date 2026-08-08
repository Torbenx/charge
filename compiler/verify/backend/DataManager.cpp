#include <verify/backend/DataManager.h>

#include <verify/backend/Solver.h>

namespace verify::backend {

std::byte* allocateAndFillData(TheoryId theory, const DataManager::TheoryInfo& theoryInfo, const DataManager::CommonDataInfo& dataInfo) {
    if (theoryInfo.dataCapacity == 0)
        return nullptr;

    VERIFY(theoryInfo.valueCount <= theoryInfo.dataCapacity);
    std::allocator<std::byte> alloc;
    std::byte* result = alloc.allocate(dataInfo.requiredBytes(theoryInfo.dataCapacity));
    for (int_t i = 0; i < dataInfo.elementCount(theoryInfo.valueCount); i++) {
        dataInfo.initFunction(result + i * dataInfo.elementSize, dataInfo.elementValue(theory, i));
    }
    return result;
}

void moveData(std::byte*& data, const DataManager::CommonDataInfo& dataInfo, int_t oldCapacity, int_t newCapacity) {
    std::allocator<std::byte> alloc;
    std::byte* newData = alloc.allocate(dataInfo.requiredBytes(newCapacity));
    if (oldCapacity == 0) {
        VERIFY(data == nullptr);
    } else {
        size_t oldByteCount = dataInfo.requiredBytes(oldCapacity);
        std::copy_n(data, oldByteCount, newData);
        alloc.deallocate(data, oldByteCount);
    }
    data = newData;
}

void deallocateAndDestroyData(std::byte*& data, const DataManager::TheoryInfo& theoryInfo, const DataManager::CommonDataInfo& dataInfo) {
    if (theoryInfo.dataCapacity == 0) {
        VERIFY(data == nullptr);
        return;
    }
    for (int_t i = 0; i < dataInfo.elementCount(theoryInfo.valueCount); i++) {
        dataInfo.destroyFunction(data + i * dataInfo.elementSize);
    }
    std::allocator<std::byte> alloc;
    alloc.deallocate(data, dataInfo.requiredBytes(theoryInfo.dataCapacity));
    data = nullptr;
}

void initValue(std::byte* data, const DataManager::CommonDataInfo& dataInfo, Value value) {
    if (dataInfo.needsNewValue(value.id())) {
        dataInfo.initFunction(data + dataInfo.elementFor(value.id()) * dataInfo.elementSize, value);
    }
}

TheoryDataBase::TheoryDataBase(Solver& solver, TheoryId theory, int_t elementSize, int_t groupSize, DataInitializeFunction initFunction, DataDestroyFunction destroyFunction) {
    solver.dataManager().registerTheoryData(*this, theory, elementSize, groupSize, initFunction, destroyFunction);
}

void DataManager::registerTheoryData(TheoryDataBase& data, TheoryId theory, int_t elementSize, int_t groupSize, DataInitializeFunction initFunction, DataDestroyFunction destroyFunction) {
    auto& info = at(theory);
    auto& dataInfo = info.datas.emplace_back(CommonDataInfo { elementSize, groupSize, initFunction, destroyFunction }, &data);
    VERIFY(data.m_pointer == nullptr);
    dataInfo.pointer = allocateAndFillData(theory, info, dataInfo);
    data.m_pointer = dataInfo.pointer;
}

SortDataBase::SortDataBase(Solver& solver, Sort sort, int_t elementSize, int_t groupSize, DataInitializeFunction initFunction, DataDestroyFunction destroyFunction) {
    solver.dataManager().registerSortData(*this, sort, elementSize, groupSize, initFunction, destroyFunction);
}

void DataManager::registerSortData(SortDataBase& data, Sort sort, int_t elementSize, int_t groupSize, DataInitializeFunction initFunction, DataDestroyFunction destroyFunction) {
    auto& info = at(sort);
    auto& dataInfo = info.datas.emplace_back(CommonDataInfo { elementSize, groupSize, initFunction, destroyFunction }, &data);
    VERIFY(data.m_table == nullptr);
    std::allocator<std::byte*> alloc;
    dataInfo.table = alloc.allocate(theories.size());
    std::uninitialized_fill_n(dataInfo.table, theories.size(), nullptr);
    for (int_t i = 0; i < (int_t)theories.size(); i++) {
        TheoryId theory = (TheoryId)i;
        if (sortOf(theory) == sort) {
            dataInfo.table[i] = allocateAndFillData(theory, at(theory), dataInfo);
        }
    }
    data.m_table = dataInfo.table;
}

Value DataManager::newValue(TheoryId theory, int_t count) {
    VERIFY(count > 0);
    auto& theoryInfo = at(theory);
    auto& sortInfo = at(sortOf(theory));
    int_t firstValueId = theoryInfo.valueCount;
    theoryInfo.valueCount += count;
    if (theoryInfo.valueCount > theoryInfo.dataCapacity) {
        int_t oldCapacity = theoryInfo.dataCapacity;
        int_t newCapacity = std::max<int_t>(oldCapacity * 2, 4);
        theoryInfo.dataCapacity = newCapacity;
        for (auto& dataInfo : theoryInfo.datas) {
            moveData(dataInfo.pointer, dataInfo, oldCapacity, newCapacity);
            dataInfo.base->m_pointer = dataInfo.pointer;
        }
        for (auto& dataInfo : sortInfo.datas) {
            moveData(dataInfo.table[std::to_underlying(theory)], dataInfo, oldCapacity, newCapacity);
        }
    }
    for (auto& dataInfo : theoryInfo.datas) {
        for (int_t i = 0; i < count; i++)
            initValue(dataInfo.pointer, dataInfo, Value(theory, firstValueId + i));
    }
    for (auto& dataInfo : sortInfo.datas) {
        for (int_t i = 0; i < count; i++)
            initValue(dataInfo.table[std::to_underlying(theory)], dataInfo, Value(theory, firstValueId + i));
    }
    return Value(theory, firstValueId);
}

DataManager::DataManager() { }

DataManager::~DataManager() {
    // Note: We must not access the Theory/SortDataBase since they may already be deallocated.
    for (auto& theoryInfo : theories) {
        if (theoryInfo.dataCapacity == 0)
            continue;
        for (auto& dataInfo : theoryInfo.datas) {
            VERIFY(dataInfo.pointer != nullptr);
            deallocateAndDestroyData(dataInfo.pointer, theoryInfo, dataInfo);
        }
    }
    for (auto& sortInfo : sorts) {
        for (auto& dataInfo : sortInfo.datas) {
            for (int_t i = 0; i < (int_t)theories.size(); i++) {
                if (dataInfo.table[i] != nullptr) {
                    deallocateAndDestroyData(dataInfo.table[i], theories[i], dataInfo);
                }
            }
            std::allocator<std::byte*> alloc;
            alloc.deallocate(dataInfo.table, theories.size());
            dataInfo.table = nullptr;
        }
    }
}

}