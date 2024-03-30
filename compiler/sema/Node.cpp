#include <sema/Node.h>

namespace sema {

void Node::validateTreeProperty() {
    auto it = reverseChildren().begin();
    std::advance(it, childrenCount());
    VERIFY(it == reverseChildren().end());
}

}