#include <PageBumpAllocator.h>

#include <Windows.h>

namespace allocdetail {

void* reserveMemory(int_t capacityInBytes) {
    void* result = VirtualAlloc(nullptr, capacityInBytes, MEM_RESERVE, PAGE_NOACCESS);
    VERIFY(result != nullptr);
    return result;
}

void commitMemory(void* addr) {
    void* result = VirtualAlloc(addr, COMMIT_GRANULARITY, MEM_COMMIT, PAGE_READWRITE);
    VERIFY(result == addr);
}

void releaseMemory(void* base, [[maybe_unused]] int_t capacityInBytes) {
    auto result = VirtualFree(base, 0, MEM_RELEASE);
    VERIFY(result != 0);
}

}