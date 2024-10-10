#pragma once

#include <types.h>

template<typename T>
struct OperatorOrdering {
    std::strong_ordering operator()(const T& a, const T& b) const { return a <=> b; }
};

template<typename T, typename Compare = OperatorOrdering<T>, typename... Args>
struct FlatSet {
    static FlatSet unionOf(Args... args, const FlatSet& a, const FlatSet& b) {
        FlatSet result;
        result.s = Storage(a.size() + b.size());
        result.s.end = forwardUnion(args..., result.s.begin, a.s.span(), b.s.span());
        return result;
    }

    static FlatSet intersectionOf(Args... args, const FlatSet& a, const FlatSet& b) {
        FlatSet result;
        result.s = Storage(std::min(a.size(), b.size()));
        result.s.end = intersection(args..., result.s.begin, a.s.span(), b.s.span());
        return result;
    }

    void unionWith(Args... args, const FlatSet& other) {
        if (s.capacity < s.begin) {
            if (s.begin - s.capacity < other.size()) {
                *this = unionOf(args..., *this, other);
            } else {
                T* capacityEnd = s.end;
                s.end = forwardUnion(args..., s.capacity, s.span(), other.s.span());
                s.begin = s.capacity;
                s.capacity = capacityEnd;
            }
        } else {
            if (s.capacity - s.end < other.size()) {
                *this = unionOf(args..., *this, other);
            } else {
                T* capacityBegin = s.begin;
                s.begin = backwardUnion(args..., s.capacity - 1, s.span(), other.s.span()) + 1;
                s.end = s.capacity;
                s.capacity = capacityBegin;
            }
        }
    }

    void intersectionWith(Args... args, const FlatSet& other) {
        if (s.capacity < s.begin) {
            T* capacityEnd = s.end;
            s.end = intersection(args..., s.capacity, s.span(), other.s.span());
            s.begin = s.capacity;
            s.capacity = capacityEnd;
        } else {
            s.end = intersection(args..., s.begin, s.span(), other.s.span());
        }
    }

    int_t capacity() const {
        if (s.capacity < s.begin)
            return s.end - s.capacity;
        else
            return s.capacity - s.begin;
    }

    int_t size() const { return s.end - s.begin; }

    const T* begin() const { return s.begin; }
    const T* end() const { return s.end; }

    bool operator==(const FlatSet& other) const {
        if (size() != other.size())
            return false;
        return std::equal(s.begin, s.end, other.s.begin);
    }

    FlatSet() = default;
    FlatSet(const FlatSet& other)
        : FlatSet(other.s.span()) { }
    FlatSet& operator=(const FlatSet& other) {
        *this = FlatSet(other.s.span());
        return *this;
    }
    FlatSet(FlatSet&& other) = default;
    FlatSet& operator=(FlatSet&&) = default;
    ~FlatSet() = default;

    static FlatSet fromSorted(std::span<const T> sorted) {
        return FlatSet(sorted);
    }

private:
    struct Members {
        T* begin = nullptr;
        T* end = nullptr;
        T* capacity = nullptr;

        std::span<T> span() const { return { begin, end }; }
    };
    struct Storage : Members {
        Storage() = default;
        Storage(int_t requiredSize) {
            std::allocator<T> alloc;
            size_t size = std::bit_ceil<size_t>(requiredSize);
            this->begin = this->end = alloc.allocate(size);
            this->capacity = this->begin + size;
        }
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
            return *this;
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

    explicit FlatSet(std::span<const T> sorted) {
        s = Storage(sorted.size());
        s.end = std::copy(sorted.begin(), sorted.end(), s.begin);
    }

    static T* intersection(Args... args, T* insert, std::span<const T> a, std::span<const T> b) {
        auto aIt = a.begin();
        auto bIt = b.begin();
        Compare comp;
        for (;;) {
            if (aIt == a.end() || bIt == b.end())
                break;

            auto ordering = comp(args..., *aIt, *bIt);
            if (ordering < 0) {
                ++aIt;
            } else if (ordering > 0) {
                ++bIt;
            } else {
                *insert = *aIt;
                ++insert;
                ++aIt;
                ++bIt;
            }
        }
        return insert;
    }

    static T* forwardUnion(Args... args, T* insert, std::span<const T> a, std::span<const T> b) {
        auto aIt = a.begin();
        auto bIt = b.begin();
        Compare comp;
        for (;;) {
            if (aIt == a.end()) {
                insert = std::copy(bIt, b.end(), insert);
                break;
            }
            if (bIt == b.end()) {
                insert = std::copy(aIt, a.end(), insert);
                break;
            }
            auto ordering = comp(args..., *aIt, *bIt);
            if (ordering < 0) {
                *insert = *aIt;
                ++aIt;
            } else if (ordering > 0) {
                *insert = *bIt;
                ++bIt;
            } else {
                *insert = *aIt;
                ++aIt;
                ++bIt;
            }
            ++insert;
        }
        return insert;
    }

    static T* backwardUnion(Args... args, T* target, std::span<const T> a, std::span<const T> b) {
        auto aIt = a.rbegin();
        auto bIt = b.rbegin();
        auto insert = std::span<T>(target, 1).rbegin();
        Compare comp;
        for (;;) {
            if (aIt == a.rend()) {
                insert = std::copy(bIt, b.rend(), insert);
                break;
            }
            if (bIt == b.rend()) {
                insert = std::copy(aIt, a.rend(), insert);
                break;
            }
            auto ordering = comp(args..., *aIt, *bIt);
            if (ordering > 0) {
                *insert = *aIt;
                ++aIt;
            } else if (ordering < 0) {
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