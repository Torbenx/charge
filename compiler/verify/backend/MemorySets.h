#pragma once

#include <verify/backend/MemoryLocationSets.h>

namespace verify::backend {

//! Basic prefix impl for member expressions
/*!
A word is spelled by the normal form of its expression
*/
struct MemberPrefixes {
    using Letter = Member;
    using WordKey = Member;

    static constexpr UseKind wordUse = UseKind::MemoryPrefixWord;
    static constexpr TypedReasonKind<PrefixHitData> hitReason = makeTypedReasonKind<ReasonKind::MemberPrefixHit>();
    static constexpr Member invalidLetter = (Member)INVALID_VALUE;

    static size_t hashLetter(Member letter) { return std::bit_cast<uint32_t>((Value)letter); }
    static bool letterStable(Member letter) { return letter.literal(); }

    Value watchedValue(Member expression) const { return expression; }
    void appendLetters(Solver&, Member expression, std::vector<Member>& out);
    void explainLetters(Solver&, Member expression, ClauseBuilder&);

    bool raisesConflict(PrefixHitSide<Member>, PrefixHitSide<Member>, bool) const {
        // All hits are genuine conflicts
        return true;
    }
};

struct MemorySets : MemoryLocationSets<MemorySets, MemberPrefixes> {
    static constexpr Params PARAMS = {
        .setSort = Sort::MemorySet,
        .declarationsShareElementReason = makeTypedReasonKind<ReasonKind::MemoryDeclarationsShareElement>(),
        .pendingRewriteUse = UseKind::MemorySetPendingContainment,
        .representativeRewriteUse = UseKind::MemorySetRepresentative,
    };
    using Base = MemoryLocationSets<MemorySets, MemberPrefixes>;
    using SetHandle = MemorySet;

    MemorySets(Solver&);

    MemorySet set(Solver&, MemoryLocation location);
    MemorySet set(Solver& solver, MemoryDeclaration declaration, Member member) {
        return set(solver, { declaration, member });
    }

    MemoryLocation locationOf(MemorySet set) const {
        VERIFY(set.theory() == TheoryId::MemoryLocationSets);
        return setInfos[set].location;
    }
    void addWords(Solver& solver, Prefixes& prefixes, ElementId element, Containment cont) {
        // If an element is in a location set but not in the location set of some prefix of it, thats a conflict.
        auto role = cont.contained() ? PrefixRole::Path : PrefixRole::Candidate;
        prefixes.addWord(solver, locationOf((MemorySet)cont.set()).member, element, cont, role);
    }

private:
    struct SetInfo {
        SetInfo() = default;
        MemoryLocation location { MemoryDeclaration(INVALID_VALUE) };
    };

    // Note: The location key could be obtained from the stored value
    std::unordered_map<MemoryLocation, MemorySet, MemoryLocationHash> sets;

    TheoryData<SetInfo, TheoryId::MemoryLocationSets> setInfos;
};

}