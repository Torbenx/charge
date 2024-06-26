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
        uint32_t tracePosition = -1; //! Position of the trace entry where this value ceased to be a root or -1 if it is a root
        std::vector<TreeNode> tree;
        std::vector<Watch> watches;
    };

    StandardEquality(Solver&);

    void link(Solver& solver, int_t eqId, bool propagate);

    bool connected(Value a, Value b) {
        return equalityInfo(a).root == equalityInfo(b).root;
    }

    virtual EqualityInfo& equalityInfo(Value value) = 0;

    void propagateFalseAssignment(Solver&, BooleanValue) override;
    void reapplyFalseAssignment(Solver&, BooleanValue) override;
    void unapplyFalseAssignment(Solver&, BooleanValue) override;

    void checkInvariances();

    bool testReason(Solver&, const Reason&) override;
    static Link linkFromReason(const Reason&);
    Reason linkToReason(Link l) const;
    ClauseAndIndex reasonToClause(Solver&, const Reason&) override;
    void newDecisionLevel(Solver&) override;
    void backtrack(Solver&) override;

private:
    void onNewVariable(Solver&, int_t eqId) override;

    void path(Solver&, Value a, Value b, std::vector<BooleanValue>&);
    void pathInTree(Solver&, Value a, Value b, std::vector<BooleanValue>&);

    struct TraceEntry {
        Link link;
        Link roots; //!< The root of the source and target respectively
    };

    std::vector<TraceEntry> trace;
    std::vector<uint32_t> decisionPoints; //!< Trace sizes at the respective decision levels

    //! Copy of the trace at the time of backtracking. Used to reconstruct paths after backtracking
    std::vector<TraceEntry> backtrackTrace;
};

}