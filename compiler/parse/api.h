#pragma once

#include <types.h>

namespace sema {
struct Context;
}

namespace parse {

struct ParseException : std::exception {
    const char* what() const noexcept override;
};

void parseOrThrow(sema::Context&);

}