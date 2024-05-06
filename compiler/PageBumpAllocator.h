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
        offsetBytes = 0;
        capacityBytes = capacity * sizeof(T);
        m_begin = (T*)allocdetail::reserveMemory(capacityBytes);
        allocdetail::commitMemory(m_begin);
    }

    T* begin() { return m_begin; }
    const T* begin() const { return m_begin; }
    T* end() { return ptrFromBytes(offsetBytes); }
    const T* end() const { return ptrFromBytes(offsetBytes); }
    T* data() { return m_begin; }
    const T* data() const { return m_begin; }
    T& front() { return *m_begin; }
    const T& front() const { return *m_begin; }
    T& back() { return *ptrFromBytes(offsetBytes - sizeof(T)); }
    const T& back() const { return *ptrFromBytes(offsetBytes - sizeof(T)); }
    T& operator[](int_t index) { return m_begin[index]; }
    const T& operator[](int_t index) const { return m_begin[index]; }
    int_t size() const { return offsetBytes / sizeof(T); }
    int_t capacity() const { return capacityBytes / sizeof(T); }

    T* allocate() {
        uint32_t oldOffsetBytes = offsetBytes;
        offsetBytes += sizeof(T);
        if ((oldOffsetBytes ^ offsetBytes) >= allocdetail::COMMIT_GRANULARITY) {
            grow();
        }
        return ptrFromBytes(oldOffsetBytes);
    }

    void push_back(T t) {
        T* target = allocate();
        std::construct_at(target, t);
    }

    void clear() {
        std::destroy_n(data(), size());
        offsetBytes = 0;
    }

    ~PageBumpAllocator() {
        std::destroy_n(data(), size());
        allocdetail::releaseMemory(data(), capacity() * sizeof(T));
    }

private:
    T* m_begin = nullptr;
    uint32_t offsetBytes = 0;
    uint32_t capacityBytes = 0;

    T* ptrFromBytes(uint32_t bytes) const {
        return reinterpret_cast<T*>(reinterpret_cast<std::byte*>(m_begin) + bytes);
    }

    void grow() {
        VERIFY(offsetBytes <= capacityBytes);
        allocdetail::commitMemory(ptrFromBytes(offsetBytes & ~(allocdetail::COMMIT_GRANULARITY - 1)));
    }
};