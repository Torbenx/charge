#pragma once

#include <check/Value.h>

namespace check {

struct Solver;
struct ValueTheory;

using DataInitializeFunction = void (*)(std::byte*, Value);
using DataDestroyFunction = void (*)(std::byte*);
template<typename T>
struct DataInitializeFunctionFor;
template<std::default_initializable T>
struct DataInitializeFunctionFor<T> {
    static constexpr DataInitializeFunction F
        = [](std::byte* target, Value) { std::construct_at(reinterpret_cast<T*>(target)); };
};
template<std::constructible_from<Value> T>
struct DataInitializeFunctionFor<T> {
    static constexpr DataInitializeFunction F
        = [](std::byte* target, Value v) { std::construct_at(reinterpret_cast<T*>(target), v); };
};
template<typename T>
struct DataDestroyFunctionFor {
    static constexpr DataDestroyFunction F
        = [](std::byte* target) { std::destroy_at(reinterpret_cast<T*>(target)); };
};

struct TheoryDataBase {
    TheoryDataBase(Solver&, ValueTheory&, int_t elementSize, DataInitializeFunction, DataDestroyFunction);
    TheoryDataBase(const TheoryDataBase&) = delete;
    TheoryDataBase(TheoryDataBase&&) = delete;

    std::byte* m_pointer = nullptr;
};

struct KindDataBase {
    KindDataBase(Solver&, ValueKind, int_t elementSize, DataInitializeFunction, DataDestroyFunction);
    KindDataBase(const KindDataBase&) = delete;
    KindDataBase(KindDataBase&&) = delete;

    std::byte* theoryData(uint8_t theoryId) const { return m_table[theoryId]; }

    std::byte** m_table = nullptr;
};

template<typename T>
struct TheoryData : private TheoryDataBase {
    TheoryData(Solver& solver, ValueTheory& theory)
        : TheoryDataBase(solver, theory, sizeof(T), DataInitializeFunctionFor<T>::F, DataDestroyFunctionFor<T>::F) { }

    T& operator[](Value v) const {
        return *(reinterpret_cast<T*>(m_pointer) + v.valueId);
    }
};

template<typename T>
struct KindData : private KindDataBase {
    KindData(Solver& solver, ValueKind kind)
        : KindDataBase(solver, kind, sizeof(T), DataInitializeFunctionFor<T>::F, DataDestroyFunctionFor<T>::F) { }

    T& operator[](Value v) const {
        return *(reinterpret_cast<T*>(theoryData(v.theoryId)) + v.valueId);
    }
};

}