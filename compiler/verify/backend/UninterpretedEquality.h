#pragma once

#include <verify/backend/SatCore.h>
#include <verify/backend/Solver.h>

namespace verify::backend {

struct UninterpretedEqualityParams {
    ValueKind valueKind;
    TheoryId theory;
    TypedReasonKind<EmptyReasonData> equalityReason;
    TypedReasonKind<DisequalityReason> disequalityReason;
    TypedReasonKind<DisequalityReason> disequalityByAlwaysDisqualReason;
};

namespace theory_params {

#define UNINTERPRETED_EQUALITY_THEORY(valueKind, memberName)                       \
    inline constexpr UninterpretedEqualityParams eq##valueKind = {                 \
        ValueKind::valueKind,                                                      \
        TheoryId::valueKind##Equality,                                             \
        makeTypedReasonKind<ReasonKind::valueKind##Equality>(),                    \
        makeTypedReasonKind<ReasonKind::valueKind##Disequality>(),                 \
        makeTypedReasonKind<ReasonKind::valueKind##DisequalityByAlwaysDisequal>(), \
    };
#include <verify/backend/theories.inc>

}

struct UninterpretedEquality {
    UninterpretedEquality(Solver&, const UninterpretedEqualityParams&);

    BooleanValue makeEquality(PairHandle pair) const {
        return { params.theory, pair.pairId() * 2 };
    }
    PairHandle pairOf(BooleanValue equality) const {
        return { params.valueKind, equality.id() / 2 };
    }

    void propagateEqual(Solver& solver, PairHandle eqPair);
    void propagateDisequal(Solver& solver, PairHandle diseqPair);

    void checkInvariances(Solver&);

    bool testReason(Solver&, BooleanValue, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&);
    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

    void newPair(Solver&, PairHandle);

    void forEachParentOf(Value value, auto&& callback, uint32_t traceSizeLimit = std::numeric_limits<uint32_t>::max());
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
        std::optional<TracePosition> tracePosition; //!< Position of the trace entry where this value ceased to be a root.
        std::vector<TreeNode> tree;
        std::vector<Edge> edges;
        std::vector<uint32_t> disequalities; //!< Sorted list of disequalities this node is a part of
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

    UninterpretedEqualityParams params;

    KindData<EqualityInfo> equalityInfos;

    std::vector<EqualityTraceEntry> equalityTrace;
    std::vector<DisequalityTraceEntry> disequalityTrace;
    std::vector<uint32_t> equalityDecisionPoints; //!< Trace sizes at the respective decision levels
    std::vector<uint32_t> disequalityDecisionPoints; //!< Trace sizes at the respective decision levels
};

inline void UninterpretedEquality::forEachParentOf(Value value, auto&& callback, uint32_t traceSizeLimit) {
    for (;;) {
        callback(value);
        const auto& valueInfo = infoFor(value);
        if (!valueInfo.tracePosition.has_value())
            break;

        uint32_t index = valueInfo.tracePosition->index;
        if (index >= traceSizeLimit)
            break;
        value = equalityTrace[index].roots.source;
    }
}

inline void UninterpretedEquality::forEachEqualValue(Value value, auto&& callback) {
    const auto& tree = infoFor(infoFor(value).root).tree;
    for (const auto& node : tree) {
        callback(node.value);
    }
}

}