#include <parse/Token.h>

namespace parse {

std::string_view nameString(DataKind kind) {
    switch (kind) {
#define DATAKIND(type, name) \
    case DataKind::name:       \
        return #name;

#include <parse/tokens.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

std::string_view nameString(TokenKind kind) {
    switch (kind) {
#define TOKEN(kind, lexToken, data1, data2) \
    case TokenKind::kind:       \
        return #kind;

#include <parse/tokens.inc>
    default:
        VERIFY_NOT_REACHED();
    }
}

std::string_view nameString(ScopeKind kind) {
    switch (kind) {
#define SCOPE(kind)       \
    case ScopeKind::kind: \
        return #kind;
        ENUMERATE_SCOPE_KINDS
#undef SCOPE
    default:
        VERIFY_NOT_REACHED();
    }
}

}