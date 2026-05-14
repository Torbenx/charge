#pragma once

#include <verify/backend/Solver.h>

namespace verify::backend {

struct Types {

    enum class FrameElementKind : uint8_t {
        UnknownImpl,
        UniquePointer,
        SharedPointer,
    };

    struct FrameElement {
        FrameElementKind kind;
        Member member;
    };

    struct ImplInfo {
        std::vector<FrameElement> frame;
    };
};

}