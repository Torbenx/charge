#pragma once

#include "types.h"
#include <limits>
#include <new>

template<size_t elementAlignment>
struct aligned_t {
    uint32_t value = 0;
    aligned_t operator+(const aligned_t& other) const {
        return { value + other.value };
    }
    auto operator<=>(const aligned_t&) const = default;
    size_t bytes() const { return value * elementAlignment; }

    template<typename T, typename S>
    static aligned_t backwards_diff(T* target, S* source) {
        static_assert(alignof(T) >= elementAlignment && alignof(S) >= elementAlignment);
        int_t diff = ((std::byte*)source - (std::byte*)target) / elementAlignment;
        VERIFY(diff >= 0);
        VERIFY(diff <= (int_t)std::numeric_limits<uint32_t>::max());
        return { (uint32_t)diff };
    }
    template<typename T, typename S>
    static aligned_t forwards_diff(T* target, S* source) {
        static_assert(alignof(T) >= elementAlignment && alignof(S) >= elementAlignment);
        int_t diff = ((std::byte*)target - (std::byte*)source) / elementAlignment;
        VERIFY(diff >= 0);
        VERIFY(diff <= (int_t)std::numeric_limits<uint32_t>::max());
        return { (uint32_t)diff };
    }
};
template<size_t elementAlignment>
std::byte* operator+(std::byte* p, aligned_t<elementAlignment> a) {
    return p + (size_t)a.value * elementAlignment;
}
template<size_t elementAlignment>
std::byte* operator-(std::byte* p, aligned_t<elementAlignment> a) {
    return p - (size_t)a.value * elementAlignment;
}
template<size_t elementAlignment>
aligned_t<elementAlignment> operator+(aligned_t<elementAlignment> l, aligned_t<elementAlignment> r) {
    return { l.value + r.value };
}
template<size_t elementAlignment>
aligned_t<elementAlignment> operator-(aligned_t<elementAlignment> l, aligned_t<elementAlignment> r) {
    return { l.value - r.value };
}

template<size_t elementAlignment>
struct StreamAllocatorFields {
    using aligned_t = ::aligned_t<elementAlignment>;

    std::byte* storage = nullptr;
    aligned_t offset = { 0 };
    aligned_t capacity = { 0 };
};
template<size_t elementAlignment>
struct StreamAllocator : StreamAllocatorFields<elementAlignment> {
    using Base = StreamAllocatorFields<elementAlignment>;
    static_assert(std::has_single_bit(elementAlignment));

    StreamAllocator() { allocateStorage(128 * 1024 * 1024); }
    StreamAllocator(StreamAllocator&& other)
        : Base(other) {
        (Base&)other = {};
    }
    StreamAllocator& operator=(StreamAllocator&& other) {
        freeStorage();
        (Base&)* this = other;
        (Base&)other = {};
        return *this;
    }
    ~StreamAllocator() { freeStorage(); }

    template<typename T>
    T* allocate() {
        return (T*)allocate(alignof(T), sizeof(T));
    }
    void* allocate(int_t alignment, int_t size) {
        VERIFY(size > 0 && alignment > 0 && std::has_single_bit((size_t)alignment));
        aligned_t align = aligned(alignment);
        aligned_t begin = typename Base::aligned_t(alignmentCeil(Base::offset.value, align.value));
        aligned_t end = begin + aligned(size);
        VERIFY(end <= Base::capacity);
        Base::offset = end;
        return Base::storage + begin;
    }
    void* position() const {
        return Base::storage + Base::offset;
    }
    void* position(Base::aligned_t off) const {
        return Base::storage + off;
    }
    auto offsetOf(void* target) {
        return aligned((std::byte*)target - Base::storage);
    }

private:
    void freeStorage() {
        if (Base::storage == nullptr)
            return;
        operator delete(Base::storage, Base::capacity.bytes(), std::align_val_t(elementAlignment));
        (Base&)* this = {};
    }
    void allocateStorage(size_t sizeInBytes) {
        freeStorage();
        Base::offset = { 0 };
        Base::capacity = aligned(sizeInBytes);
        Base::storage = (std::byte*)operator new(Base::capacity.bytes(), std::align_val_t(elementAlignment));
    }
    static constexpr Base::aligned_t aligned(size_t bytes) {
        return { (uint32_t)((bytes + elementAlignment - 1) / elementAlignment) };
    }
};
template<typename T>
struct HomogeneousStreamAllocator : StreamAllocator<sizeof(T)> {
private:
    using Base = StreamAllocator<sizeof(T)>;
    using Base::allocate;

public:
    T* position() const { return (T*)Base::position(); }

    int_t size() const { return Base::offset.value; }
    const T* data() const {
        return (T*)Base::storage;
    }
    const T& operator[](int_t i) const {
        return *(data() + i);
    }
    const T* begin() const { return data(); }
    const T* end() const { return data() + size(); }
    const T& back() const { return (*this)[size() - 1]; }
    void truncate(int_t newSize) {
        VERIFY(newSize <= size());
        std::destroy(begin() + newSize, end());
        Base::offset.value = newSize;
    }
    void emit(const T& value) {
        std::construct_at(Base::template allocate<T>(), value);
    }
};