#pragma once

#include <check/Value.h>

namespace check {

namespace LoadSetDetail {
    template<typename T>
    struct Data {
        Load load;
        T data;
    };

    template<>
    struct Data<void> {
        Load load;
    };
}

template<typename Impl, typename T>
struct LoadSet : private FlatTreeSetDetail::Base<LoadSet<Impl, T>, LoadSetDetail::Data<T>> {
private:
    using Data = LoadSetDetail::Data<T>;
    using Base = FlatTreeSetDetail::Base<LoadSet<Impl, T>, Data>;

public:
    uint32_t get(Solver& solver, MemoryLocation loc, CodePosition pos) {
        return Base::get(solver, loc, pos);
    }
    Load loadAt(uint32_t index) { return Base::at(index).load; }
    auto& at(uint32_t index)
        requires(!std::is_void_v<T>)
    { return Base::at(index).data; }
    using Base::label;
    using Base::size;

private:
    Impl* impl() { return static_cast<Impl*>(this); }

    std::strong_ordering compare(std::same_as<Solver> auto& solver, MemoryLocation loc, CodePosition pos, const Data& data) {
        return solver.compare({ loc, pos }, data.load);
    }

    uint32_t makeNode(Solver&, MemoryLocation loc, CodePosition pos, TreeLabel label) {
        uint32_t nextHandle = Base::nextNodeHandle();
        if constexpr (std::is_void_v<T>) {
            impl()->makeData(nextHandle);
            return Base::makeNode(label, Data { .load = { loc, pos } });
        } else {
            return Base::makeNode(label, Data { .load = { loc, pos }, .data = impl()->makeData(nextHandle) });
        }
    }

    friend Base;
};

}