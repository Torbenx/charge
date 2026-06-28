#pragma once

#include <verify/backend/Value.h>

namespace verify::backend {

struct Solver;

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
    TheoryDataBase(Solver&, TheoryId, int_t elementSize, int_t groupSize, DataInitializeFunction, DataDestroyFunction);
    TheoryDataBase(const TheoryDataBase&) = delete;
    TheoryDataBase(TheoryDataBase&&) = delete;

    std::byte* m_pointer = nullptr;
};

struct KindDataBase {
    KindDataBase(Solver&, ValueKind, int_t elementSize, int_t groupSize, DataInitializeFunction, DataDestroyFunction);
    KindDataBase(const KindDataBase&) = delete;
    KindDataBase(KindDataBase&&) = delete;

    std::byte* theoryData(TheoryId theory) const { return m_table[std::to_underlying(theory)]; }

    std::byte** m_table = nullptr;
};

template<typename T, TheoryId theory = TheoryId::COUNT, int_t groupSize = 1>
struct TheoryData : private TheoryDataBase {
    static_assert(groupSize > 0);
    TheoryData(Solver& solver) requires (theory < TheoryId::COUNT)
        : TheoryData(solver, theory) { }
    TheoryData(Solver& solver, TheoryId dynamicTheory)
        : TheoryDataBase(solver, dynamicTheory, sizeof(T), groupSize, DataInitializeFunctionFor<T>::F, DataDestroyFunctionFor<T>::F) { }

    T& operator[](Value v) const {
        return *(reinterpret_cast<T*>(m_pointer) + v.id() / groupSize);
    }
};

template<typename T, ValueKind kind = ValueKind::COUNT>
struct KindData : private KindDataBase {
    KindData(Solver& solver) requires (kind < ValueKind::COUNT)
        : KindData(solver, kind) { }
    KindData(Solver& solver, ValueKind dynamicKind)
        : KindDataBase(solver, dynamicKind, sizeof(T), 1, DataInitializeFunctionFor<T>::F, DataDestroyFunctionFor<T>::F) { }

    T& operator[](Value v) const {
        return *(reinterpret_cast<T*>(theoryData(v.theory())) + v.id());
    }
};

}