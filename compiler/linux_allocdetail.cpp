#include <PageBumpAllocator.h>

#include <sys/mman.h>

namespace allocdetail {

void* reserveMemory(int_t capacityInBytes) {
    void* result = mmap(nullptr, capacityInBytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    VERIFY(result != nullptr);
    return result;
}

void commitMemory(void* addr) {
    int ret = mprotect(addr, COMMIT_GRANULARITY, PROT_READ | PROT_WRITE);
    VERIFY(ret == 0);
}

void releaseMemory(void* base, int_t capacityInBytes) {
    int ret = munmap(base, capacityInBytes);
    VERIFY(ret == 0);
}

}