#include <sema/Node.h>

namespace sema {

void Node::validateTreeProperty() {
    auto it = reverseChildren().begin();
    std::advance(it, childrenCount());
    VERIFY(it == reverseChildren().end());
}

std::string_view nameString(NodeKind kind) {
    switch (kind) {
#define KIND(kind, cat)       \
    case NodeKind::kind: \
        return #kind;

        ENUMERATE_NODE_KINDS

#undef KIND
    default:
        VERIFY_NOT_REACHED();
    }
}

}