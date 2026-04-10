#include <verify/backend/Value.h>

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

}