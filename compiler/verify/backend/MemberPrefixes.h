#pragma once

#include <verify/backend/PrefixIndex.h>

namespace verify::backend {

struct MemberPrefixes;

template<>
struct prefix_index_traits<MemberPrefixes> {
    using Letter = Member;
    using WordKey = Member;

    static constexpr UseKind wordUse = UseKind::MemberPrefixWord;
    static constexpr TypedReasonKind<PrefixHitData> hitReason = makeTypedReasonKind<ReasonKind::MemberPrefixHit>();
    static constexpr Member invalidLetter = (Member)INVALID_VALUE;

    static size_t hashLetter(Member letter) { return std::bit_cast<uint32_t>((Value)letter); }
};

//! The prefix index of member expressions
/*!
A word is spelled by the normal form of its expression
*/
struct MemberPrefixes : PrefixIndex<MemberPrefixes> {
    explicit MemberPrefixes(Solver&) { }

    Value watchedValue(Member expression) const { return expression; }
    void appendLetters(Solver&, Member expression, std::vector<Member>& out);
    void explainLetters(Solver&, Member expression, ClauseBuilder&);
};

}
