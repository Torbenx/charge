#pragma once

#include <FlatTreeSet.h>
#include <verify/backend/SatCore.h>

#include <queue>

namespace verify::backend {

struct Members {
    Members(Solver&);

    std::vector<Member> rewrite(Member m);

    static BooleanValue makeEquality(PairHandle handle) {
        return (BooleanValue)encodePairTheoryValue<TheoryId::MemberEquality>(handle);
    }
    static PairHandle pairOf(BooleanValue equality) {
        return decodePairTheoryValue<TheoryId::MemberEquality>(equality);
    }

    Member compose(Solver&, std::span<const Member> expr);
    std::span<const Member> compositeMember(Member m) {
        VERIFY(m.composite());
        return compositeMembers.at(m.id());
    }
    uint32_t compositeLabel(Member m) {
        VERIFY(m.composite());
        return compositeMembers.label(m.id());
    }

    std::strong_ordering rewriteOrder(Solver&, std::span<const Member> a, std::span<const Member> b);

    void newPair(Solver&, PairHandle);
    void propagateEqual(Solver&, PairHandle);

    bool testReason(Solver&, BooleanValue, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&);

    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

private:
    using RewriteTracePosition = TracePosition;
    using AssignedPairTracePosition = TracePosition;

    struct VariableInfo {
        std::optional<RewriteTracePosition> tracePos;
        std::vector<Member> rewriteExpression; //!< Only meaningful when hasRewrite() or hadRewrite() is true
        std::vector<Member> currentRewrite;
        std::vector<RewriteTracePosition> rewriteUses;
        std::vector<PairHandle> pairUses;

        VariableInfo(Value m)
            : currentRewrite { (Member)m } { }

        bool hasRewrite() const { return tracePos.has_value(); }
    };

    struct RewriteTraceEntry {
        std::vector<Member> targets;
        PairHandle rewritePair;
    };

    struct PairInfo {
        std::optional<RewriteTracePosition> rewrite;
        std::optional<AssignedPairTracePosition> equality;
        std::optional<AssignedPairTracePosition> disequality;

        bool assignedOrRewritten() const {
            return rewrite.has_value() || equality.has_value() || disequality.has_value();
        }
    };

    struct CompositeMembers : FlatTreeSetDetail::Base<CompositeMembers, std::vector<Member>> {
        uint32_t get(Solver&, std::vector<Member>);

    private:
        friend Base;
        uint32_t makeNode(Solver&, std::vector<Member>, TreeLabel);
        std::strong_ordering compare(Solver&, std::span<const Member>, std::span<const Member>);
    };

    struct PairHandleCompare {
        bool operator()(PairHandle a, PairHandle b) const { return a.pairId() < b.pairId(); }
    };

    static void reduce(std::vector<Member>& a, std::vector<Member>& b);

    void addUses(Member m, PairHandle);

    void markUsesAsDirty(VariableInfo&);
    void addRewrite(Member target, PairHandle pair, std::vector<Member> expression);
    void addIdentityRewrite(std::vector<Member> targets, PairHandle pair);
    void updateRewrite(VariableInfo&);
    void updateRewrites();

    void assignEqual(Solver&, PairHandle);
    void assignDisequal(Solver&, PairHandle);
    void updatePair(Solver&, PairHandle);
    void grind(Solver&);

    void explainRewrite(Solver&, Member m, ClauseBuilder& clause);

    VariableInfo& infoFor(Member m) {
        VERIFY(m.variable());
        return variables[m];
    }

    TheoryData<PairInfo, TheoryId::MemberEquality, 2> pairs;
    CompositeMembers compositeMembers;
    KindData<VariableInfo, ValueKind::Member> variables;
    std::vector<RewriteTraceEntry> rewriteTrace;
    std::vector<PairHandle> assignedPairTrace;
    std::priority_queue<RewriteTracePosition> dirtyRewrites;
    std::priority_queue<PairHandle, std::vector<PairHandle>, PairHandleCompare> dirtyPairs;
    std::vector<uint32_t> rewriteDecisionPoints;
    std::vector<uint32_t> assignedPairDecisionPoints;
};

}