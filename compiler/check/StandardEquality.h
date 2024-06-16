#pragma once

#include <check/SatSolver.h>

namespace check {

struct StandardEquality : SimpleBooleanTheory<> {
    struct Link {
        Value source;
        Value target;

        bool operator==(const Link&) const = default;
    };

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
    };

    struct EqualityInfo {
        EqualityInfo(Value v)
            : root(v), index(-1) { }

        Value root;
        int32_t index;
        std::vector<TreeNode> tree;
    };

    using SimpleBooleanTheory<>::SimpleBooleanTheory;

    std::vector<Link> path(Value a, Value b);

    void link(Value source, Value target);

    bool connected(Value a, Value b) {
        return equalityInfo(a).root == equalityInfo(b).root;
    }

    virtual EqualityInfo& equalityInfo(Value value) = 0;

    void assignFalse(Solver&, BooleanValue) override {
        /* TODO */
    }
    void revertFalseAssignment(Solver&, BooleanValue) override {
        /* TODO */
    }

    std::string formatPositiveLiteral(Solver& solver, int_t eqId) override {
        auto eq = equalities[eqId];
        return fmt::format("({} == {})", solver.formatValue(eq.source), solver.formatValue(eq.target));
    }
    std::string formatNegativeLiteral(Solver& solver, int_t eqId) override {
        auto eq = equalities[eqId];
        return fmt::format("({} != {})", solver.formatValue(eq.source), solver.formatValue(eq.target));
    }

private:
    std::vector<Link> equalities;
    std::vector<Link> trace;
};

}