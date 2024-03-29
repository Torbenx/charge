#pragma once

#include <sema/Node.h>

#include <vector>

namespace sema {

struct InsertOperation {

    void insertChild() {
        VERIFY(!parent->primary());
        parent->u.compound.childrenCount += 1;
        parent->u.compound.subTreeSize += 1;
    }

    void consumeCurrent() {
        parent->u.compound.subTreeSize += 1;
    }

    void advance() {
        currentChild = precedingChild;
        if (precedingChild != end)
            ++precedingChild;
    }

    bool atEnd() const { return currentChild == end; }

private:
    struct Insertion {
        Node* after;
        Node newNode;
    };

    Node* parent;
    ChildrenIterator currentChild;
    ChildrenIterator end;
    ChildrenIterator precedingChild;
    std::vector<Insertion> insertions;
};

}