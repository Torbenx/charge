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
    void applyEqual(Solver&, PairHandle, bool propagate);

    bool testReason(Solver&, BooleanValue, const Reason&);
    ClauseAndIndex reasonToClause(Solver&, BooleanValue, const Reason&);

    void newDecisionLevel(Solver&);
    void backtrack(Solver&);

private:
    using RewriteTracePosition = TracePosition;
    using IdentityRewriteTracePosition = TracePosition;
    using DecidedPairTracePosition = TracePosition;

    struct VariableInfo {
        uint32_t backtrackCounter = 0;
        std::optional<RewriteTracePosition> rwPos;
        std::optional<IdentityRewriteTracePosition> idRwPos;
        std::optional<PairHandle> rewritePair; //!< Only meaningful when hasRewrite() or hadRewrite() is true
        std::vector<Member> rewriteExpression; //!< Only meaningful when hasRewrite() or hadRewrite() is true
        std::vector<Member> currentRewrite;
        std::vector<RewriteTracePosition> rewriteUses;
        std::vector<PairHandle> pairUses;

        VariableInfo(Value m)
            : currentRewrite { (Member)m } { }

        bool hasRewrite() const { return rwPos.has_value() || idRwPos.has_value(); }
        bool hadRewrite(int_t backtrackCounter) const {
            return hasRewrite() || (int_t)this->backtrackCounter == backtrackCounter;
        }
    };

    struct IdentityRewriteTraceEntry {
        std::vector<Member> targets;
        PairHandle rewritePair;
    };

    struct PairInfo {
        std::optional<RewriteTracePosition> rewrite;
        std::optional<IdentityRewriteTracePosition> identityRewrite;
        std::optional<DecidedPairTracePosition> equality;
        std::optional<DecidedPairTracePosition> disequality;

        bool decidedOrRewritten() const {
            return rewrite.has_value() || identityRewrite.has_value() || equality.has_value() || disequality.has_value();
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

    void decideEqual(Solver&, PairHandle, bool propagate);
    void decideDisequal(Solver&, PairHandle, bool propagate);
    void updatePair(Solver&, PairHandle, bool propagate);
    void grind(Solver&, bool propagate);

    void explainRewrite(Solver&, Member m, ClauseBuilder& clause);

    VariableInfo& infoFor(Member m) {
        VERIFY(m.variable());
        return variables[m];
    }

    TheoryData<PairInfo, TheoryId::MemberEquality, 2> pairs;
    CompositeMembers compositeMembers;
    KindData<VariableInfo, ValueKind::Member> variables;
    std::vector<Member> rewriteTrace;
    std::vector<IdentityRewriteTraceEntry> identityRewriteTrace;
    std::vector<PairHandle> decidedPairTrace;
    std::priority_queue<RewriteTracePosition> dirtyRewrites;
    std::priority_queue<PairHandle, std::vector<PairHandle>, PairHandleCompare> dirtyPairs;
    std::vector<uint32_t> rewriteDecisionPoints;
    std::vector<uint32_t> identityRewriteDecisionPoints;
    std::vector<uint32_t> decidedPairDecisionPoints;
    int_t backtrackCounter = 0;
};

}