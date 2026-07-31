#pragma once

#include <verify/backend/SatCore.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>
#include <verify/backend/Use.h>

#include <variant>

namespace verify::backend {

struct UninterpretedEqualityParams {
    Sort sort;
    TheoryId theory;
    TypedReasonKind<EmptyReasonData> equalityReason;
    TypedReasonKind<DisequalityReason> disequalityReason;
    TypedReasonKind<DisequalityReason> disequalityByAlwaysDisqualReason;
};

namespace theory_params {

#define UNINTERPRETED_EQUALITY_THEORY(sort, memberName)                       \
    inline constexpr UninterpretedEqualityParams eq##sort = {                 \
        Sort::sort,                                                           \
        TheoryId::sort##Equality,                                             \
        makeTypedReasonKind<ReasonKind::sort##Equality>(),                    \
        makeTypedReasonKind<ReasonKind::sort##Disequality>(),                 \
        makeTypedReasonKind<ReasonKind::sort##DisequalityByAlwaysDisequal>(), \
    };
#include <verify/backend/theories.inc>

}

struct UninterpretedEquality {
    UninterpretedEquality(Solver&, const UninterpretedEqualityParams&);

    Bool makeEquality(PairHandle pair) const {
        return { params.theory, pair.pairId() * 2 };
    }
    PairHandle pairOf(Bool equality) const {
        return { params.sort, equality.id() / 2 };
    }

    void propagateEqual(Solver& solver, PairHandle eqPair);
    void propagateDisequal(Solver& solver, PairHandle diseqPair);

    void checkInvariances(Solver&);

    bool testReason(Solver&, Bool, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, Bool, const Reason&);
    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

    void newPair(Solver&, PairHandle);

    void addUse(Solver&, Value value, Use use);

    //! Justify that \p a and \p b are equal by adding the negated equalities of a connecting path
    /*!
    The values must be connected or the same value.
    */
    void explainEqual(Solver& solver, Value a, Value b, ClauseBuilder& clause) { path(solver, a, b, clause); }

    void forEachParentOf(Value value, auto&& callback, TracePosition positionLimit = TracePosition(limits::max));
    void forEachEqualValue(Value value, auto&& callback);

    Value rewrite(Value v) { return infoFor(v).root; }

private:
    struct EqualityInfo {
        struct TreeNode {
            Value value;

            bool operator==(const TreeNode&) const = default;
        };

        //! Used to detect when two values become equal
        /*!
        There are two (active) edges per equality literal defined by this theory.
        When \p otherValue belongs to the same tree as the edge then the equality given by \p pair is implied.
        */
        struct Edge {
            Value otherValue;
            PairHandle pair;

            bool operator==(const Edge&) const = default;
        };

        explicit EqualityInfo(Value v)
            : root(v) { }

        Value root;
        int32_t treeOffset = -1; //!< Offset in root's tree
        uint32_t edgesOffset = 0; //!< Offset of this values edges in root's edges
        uint32_t usesOffset = 0; //!< Offset of this values uses in root's uses
        std::optional<TracePosition> tracePosition; //!< Position of the trace entry where this value ceased to be a root.
        std::vector<TreeNode> tree;
        std::vector<Edge> edges;
        std::vector<uint32_t> disequalities; //!< Sorted list of disequalities this node is a part of

        //! The uses registered for the values of this tree
        /*!
        A new use is only ever appended to the back of the list of the current root.
        That is enough because a use never outlives the links that placed it in this tree.
        */
        std::vector<Use> uses;
    };

    bool connected(Value a, Value b) {
        return infoFor(a).root == infoFor(b).root;
    }

    void addEdge(Value value, Value otherValue, PairHandle pair);

    void assignEqual(Solver&, PairHandle assignPair);
    void assignDisequal(Solver&, PairHandle assignPair, PairHandle diseqPair);
    void assignDisequalByAlwaysDisequal(Solver&, PairHandle assignPair, Value alwaysDiseqA, Value alwaysDiseqB);

    void path(Solver&, Value a, Value b, ClauseBuilder&);

    EqualityInfo& infoFor(Value v) {
        return equalityInfos[v];
    }

    struct EqualityTraceEntry {
        Pair link;
        Pair roots; //!< The root of the source and target respectively
    };

    struct DisequalityTraceEntry {
        PairHandle diseqPair;
    };

    struct UseTraceEntry {
        Value value;
        Use use;
    };

    UninterpretedEqualityParams params;

    SortData<EqualityInfo> equalityInfos;

    Trace<std::variant<EqualityTraceEntry, UseTraceEntry>> equalityUseTrace;
    Trace<DisequalityTraceEntry> disequalityTrace;
};

inline void UninterpretedEquality::forEachParentOf(Value value, auto&& callback, TracePosition positionLimit) {
    for (;;) {
        callback(value);
        const auto& valueInfo = infoFor(value);
        if (!valueInfo.tracePosition.has_value())
            break;

        TracePosition position = valueInfo.tracePosition.value();
        if (position >= positionLimit)
            break;
        value = std::get<EqualityTraceEntry>(equalityUseTrace[position]).roots.source;
    }
}

inline void UninterpretedEquality::forEachEqualValue(Value value, auto&& callback) {
    const auto& tree = infoFor(infoFor(value).root).tree;
    for (const auto& node : tree) {
        callback(node.value);
    }
}

}