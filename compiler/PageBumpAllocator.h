#pragma once

#include <types.h>

namespace allocdetail {

static constexpr size_t COMMIT_GRANULARITY = 8 * 4096;
void* reserveMemory(int_t capacityInBytes);
void commitMemory(void*);
void releaseMemory(void* base, int_t capacityInBytes);

}

template<typename T>
struct PageBumpAllocator {
    static constexpr int_t DEFAULT_CAPACITY = 2ll * 1024ll * 1024ll * 1024ll / sizeof(T);

    PageBumpAllocator(int_t capacity = DEFAULT_CAPACITY) {
        m_begin = (T*)allocdetail::reserveMemory(capacity * sizeof(T));
        offset = m_begin;
        capacityEnd = m_begin + capacity;
        allocdetail::commitMemory(m_begin);
    }

    T* begin() { return m_begin; }
    const T* begin() const { return m_begin; }
    T* end() { return offset; }
    const T* end() const { return offset; }
    T* data() { return m_begin; }
    const T* data() const { return m_begin; }
    T& front() { return *m_begin; }
    const T& front() const { return *m_begin; }
    T& back() { return *(offset - 1); }
    const T& back() const { return *(offset - 1); }
    T& operator[](int_t index) { return m_begin[index]; }
    const T& operator[](int_t index) const { return m_begin[index]; }
    int_t size() const { return offset - m_begin; }
    int_t capacity() const { return capacityEnd - m_begin; }

    T* allocate() {
        T* oldOffset = offset;
        offset += 1;
        if (((uintptr_t)oldOffset ^ (uintptr_t)offset) >= allocdetail::COMMIT_GRANULARITY) {
            grow();
        }
        return oldOffset;
    }

    void push_back(T t) {
        T* target = allocate();
        std::construct_at(target, t);
    }

    void clear() {
        std::destroy(m_begin, offset);
        offset = m_begin;
    }

    ~PageBumpAllocator() {
        std::destroy(m_begin, offset);
        allocdetail::releaseMemory(m_begin, capacity() * sizeof(T));
    }

private:
    T* m_begin = nullptr;
    T* offset = nullptr;
    T* capacityEnd = nullptr;

    [[gnu::noinline]] void grow() {
        VERIFY(offset <= capacityEnd);
        allocdetail::commitMemory((void*)((uintptr_t)offset & ~((uintptr_t)allocdetail::COMMIT_GRANULARITY - 1)));
    }
};