#include <verify/backend/Value.h>

#include <verify/backend/Reason.h>

namespace verify::backend {

std::string_view nameString(TheoryId theory) {
#define THEORY(name, valueKind) \
    case TheoryId::name:        \
        return #name;

    switch (theory) {
#include <verify/backend/theories.inc>

    default:
        VERIFY_NOT_REACHED();
    }
}

std::string_view nameString(ReasonKind kind) {
#define REASON(name, ...) \
    case ReasonKind::name:        \
        return #name;

    switch (kind) {
#include <verify/backend/theories.inc>

    default:
        VERIFY_NOT_REACHED();
    }
}

}