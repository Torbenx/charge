#pragma once

#include <types.h>

template<typename T>
struct FlatSet {
    void unionWith(const FlatSet& other) {
        if (s.capacity < s.begin) {
            if (s.begin - s.capacity < other.size()) {
                Storage newStorage = Storage(other.size() + size());
                newStorage.end = forwardUnion(newStorage.span(), s.span(), other.s.span());
                s = std::move(newStorage);
            } else {
                T* capacityEnd = s.end;
                s.end = forwardUnion({ s.capacity, capacityEnd }, s.span(), other.s.span());
                s.begin = s.capacity;
                s.capacity = capacityEnd;
            }
        } else {
            if (s.capacity - s.end < other.size()) {
                Storage newStorage = Storage(other.size() + size());
                newStorage.end = forwardUnion(newStorage.span(), s.span(), other.s.span());
                s = std::move(newStorage);
            } else {
                T* capacityBegin = s.begin;
                s.begin = backwardUnion({ capacityBegin, s.capacity }, s.span(), other.s.span()) + 1;
                s.end = s.capcity;
                s.capacity = capacityBegin;
            }
        }
    }

    int_t capacity() const {
        if (s.capacity < s.begin)
            return s.end - s.capacity;
        else
            return s.capacity - s.begin;
    }

    int_t size() const { return s.begin - s.end; }

private:
    struct Members {
        T* begin = nullptr;
        T* end = nullptr;
        T* capacity = nullptr;

        std::span<T> span() const { return { begin, end }; }
    };
    struct Storage : Members {
        Storage() = default;
        Storage(int_t requiredSize);
        Storage(const Storage&) = delete;
        Storage& operator=(const Storage&) = delete;
        Storage(Storage&& other)
            : Members(other) {
            (Members&)other = {};
        }
        Storage& operator=(const Storage&& other) {
            deallocate();
            (Members&)* this = other;
            (Members&)other = {};
        }
        void deallocate() {
            std::allocator<T> alloc;
            if (this->capacity < this->begin)
                alloc.deallocate(this->capacity, this->end - this->capacity);
            else if (this->capacity > this->end)
                alloc.deallocate(this->begin, this->capacity - this->begin);
            (Members&)* this = {};
        }
        ~Storage() {
            deallocate();
        }
    };

    Storage storageFor(int_t size) {
        if (s.capacity < s.begin) {
            if (s.end - s.capacity >= size)
                return std::move(*this);
            return Storage(size);
        }
    }

    static T* forwardUnion(std::span<T> target, std::span<const T> a, std::span<const T> b) {
        auto aIt = a.begin();
        auto bIt = b.begin();
        auto insert = target.begin();
        for (;;) {
            if (aIt == a.end()) {
                insert = std::copy(bIt, b.end(), insert);
                break;
            }
            if (bIt == b.end()) {
                insert = std::copy(aIt, a.end(), insert);
                break;
            }
            if (*aIt < *bIt) {
                *insert = *aIt;
                ++aIt;
            } else if (*bIt < aIt) {
                *insert = *bIt;
                ++bIt;
            } else {
                *insert = *aIt;
                ++aIt;
                ++bIt;
            }
            ++insert;
        }
        return &*insert;
    }

    static T* backwardUnion(std::span<T> target, std::span<const T> a, std::span<const T> b) {
        auto aIt = a.rbegin();
        auto bIt = b.rbegin();
        auto insert = target.rbegin();
        for (;;) {
            if (aIt == a.end()) {
                insert = std::copy(bIt, b.end(), insert);
                break;
            }
            if (bIt == b.end()) {
                insert = std::copy(aIt, a.end(), insert);
                break;
            }
            if (*aIt > *bIt) {
                *insert = *aIt;
                ++aIt;
            } else if (*bIt > aIt) {
                *insert = *bIt;
                ++bIt;
            } else {
                *insert = *aIt;
                ++aIt;
                ++bIt;
            }
            ++insert;
        }
        return &*insert;
    }

    Storage s;
};