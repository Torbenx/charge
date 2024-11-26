#pragma once

#include <types.h>

template<auto>
struct ReverseMemberPointer;

template<typename B, typename M, M B::*ptr>
struct ReverseMemberPointer<ptr> {
    static std::ptrdiff_t byteOffset(B* b) {
        return reinterpret_cast<std::byte*>(&((*b).*ptr)) - reinterpret_cast<std::byte*>(b);
    }
    static std::ptrdiff_t byteOffset() {
        return byteOffset(reinterpret_cast<B*>(alignof(B)));
    }
    static B* reverse(M* member) {
        return reinterpret_cast<B*>(reinterpret_cast<std::byte*>(member) - byteOffset());
    }
};