#pragma once

#include <types.h>

#include <utility>

namespace verify::backend {

enum class UseKind : uint8_t {
#define USE_KIND(name, implMember) name,
#include <verify/backend/theories.inc>

    COUNT,
};

std::string_view nameString(UseKind);

//! Represents the use of value that can be rewritten
/*!

*/
struct Use {
    static constexpr uint32_t MAX_ID = (1u << 24) - 1u;

    constexpr Use(UseKind kind, uint32_t id)
        : kindBits(std::to_underlying(kind))
        , idBits(id) {
        VERIFY(id <= MAX_ID);
    }

    constexpr UseKind kind() const { return (UseKind)kindBits; }
    constexpr uint32_t id() const { return idBits; }
    bool operator==(const Use&) const = default;

private:
    uint32_t kindBits : 8;
    uint32_t idBits : 24;
};

}
