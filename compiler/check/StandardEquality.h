#pragma once

#include <check/EqualityTheory.h>
#include <check/Reason.h>

namespace check {

struct StandardEquality : EqualityTheory, ReasonTheory {
    using Link = EqualityTheory::Link;

    struct TreeNode {
        Value value;
        uint32_t subTreeSize = 1;
        //! Node where this tree attaches to its parent tree
        /*!
        Always lies in the parent tree before this node. Stored is the offset in the parent tree.
        */
        uint32_t linkSource = 0;
        //! Node where the parent tree attaches to this tree
        /*!
        Always lies with in this subtree (linkTarget < subTreeSize). Stored is the offset from this node.
        */
        uint32_t linkTarget = 0;

        bool operator==(const TreeNode&) const = default;
    };

    //! Used to detect when two values become equal
    /*!
    The is one (active) watch per equality literal defined by this theory.
    When \p otherValue belongs to the same tree as the watch then the equality given by \p eqId is implied.
    */
    struct Watch {
        Value otherValue;
        uint32_t eqId;

        bool operator==(const Watch&) const = default;
    };

    struct EqualityInfo {
        EqualityInfo(Value v)
            : root(v) { }

        Value root;
        int32_t treeOffset = -1; //!< Offset in root's tree
        uint32_t watchOffset = 0; //!< Offset of this values watches in root's watches
        std::vector<TreeNode> tree;
        std::vector<Watch> watches;
    };

    StandardEquality(Solver&);

    void link(Solver& solver, Value source, Value target);

    bool connected(Value a, Value b) {
        return equalityInfo(a).root == equalityInfo(b).root;
    }

    virtual EqualityInfo& equalityInfo(Value value) = 0;

    void propagateFalseAssignment(Solver&, BooleanValue) override;

    void checkInvariances();

    bool testReason(Solver&, const Reason&) override;
    static Link linkFromReason(const Reason&);
    Reason linkToReason(Link l) const;
    ClauseAndIndex reasonToClause(Solver&, const Reason&) override;

private:
    void onNewVariable(Solver&, int_t eqId) override;
};

}