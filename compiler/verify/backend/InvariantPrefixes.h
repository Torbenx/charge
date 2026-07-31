#pragma once

#include <verify/backend/PrefixIndex.h>

namespace verify::backend {

//! A letter in the invariant prefix index
/*!
Every step into a member is preceded by a narrow, which is the step from a location to the leafs
strictly below it, and an invariant letter is the step from a location to the invariant leaf.
So the letters of a location with the member m1...mn are

    inclusive:   narrow m1 ... narrow mn
    exclusive:   narrow m1 ... narrow mn narrow
    invariant I: narrow m1 ... narrow mn I

Note: The leading narrow is requrired to correctly handle the empty path.
*/
struct InvariantLetter {
    static constexpr TheoryId NARROW_SENTINEL_THEORY = TheoryId::COUNT;
    static constexpr TheoryId INVARIANT_SENTINEL_THEORY = TheoryId(std::to_underlying(TheoryId::COUNT) + 1);
    static_assert(NARROW_SENTINEL_THEORY >= TheoryId::COUNT);
    static_assert(INVARIANT_SENTINEL_THEORY >= TheoryId::COUNT);
    static_assert(NARROW_SENTINEL_THEORY != INVARIANT_SENTINEL_THEORY);
    static_assert(INVARIANT_SENTINEL_THEORY != TheoryId::Invalid);

    static constexpr InvariantLetter invalid() { return { (Member)INVALID_VALUE }; }
    static constexpr InvariantLetter narrow() { return { Member(NARROW_SENTINEL_THEORY, limits::max) }; }
    static constexpr InvariantLetter member(Member member) { return { member }; }
    static constexpr InvariantLetter invariant(Invariant invariant) { return { Member(INVARIANT_SENTINEL_THEORY, invariant.id()) }; }

    constexpr bool isNarrow() const { return *this == narrow(); }
    constexpr bool isInvariant() const { return payload.theory() == INVARIANT_SENTINEL_THEORY; }
    constexpr bool isMember() const { return payload.theory() < TheoryId::COUNT; }
    constexpr Invariant invariant() const {
        VERIFY(isInvariant());
        return Invariant { payload.id() };
    }
    constexpr Member member() const {
        VERIFY(isMember());
        return payload;
    }

    bool operator==(const InvariantLetter&) const = default;

    Member payload;
};

//! Either empty, narrow or an invariant
struct InvariantWordSuffix {
    static constexpr InvariantWordSuffix empty() { return { (uint32_t)limits::max }; }
    static constexpr InvariantWordSuffix narrow() { return { (uint32_t)limits::max - 1u }; }
    static constexpr InvariantWordSuffix invariant(Invariant invariant) { return { invariant.id() }; }

    constexpr bool isEmpty() const { return *this == empty(); }
    constexpr bool isNarrow() const { return *this == narrow(); }
    constexpr bool isInvariant() const { return !isEmpty() && !isNarrow(); }
    constexpr Invariant invariant() const {
        VERIFY(isInvariant());
        return Invariant { payload };
    }

    constexpr InvariantLetter toLetter() const {
        VERIFY(!isEmpty());
        if (isNarrow())
            return InvariantLetter::narrow();
        else
            return InvariantLetter::invariant(invariant());
    }

    bool operator==(const InvariantWordSuffix&) const = default;

    uint32_t payload;
};

//! Describes a word of the invariant prefix index
struct InvariantWord {
    static InvariantWord inclusive(Member member) { return { member, InvariantWordSuffix::empty() }; }
    static InvariantWord exclusive(Member member) { return { member, InvariantWordSuffix::narrow() }; }
    static InvariantWord leaf(Member member, Invariant invariant) {
        return { member, InvariantWordSuffix::invariant(invariant) };
    }

    Member member;
    InvariantWordSuffix suffix;
};

struct InvariantPrefixes;

template<>
struct prefix_index_traits<InvariantPrefixes> {
    using Letter = InvariantLetter;
    using WordKey = InvariantWord;

    static constexpr UseKind wordUse = UseKind::InvariantPrefixWord;
    static constexpr TypedReasonKind<PrefixHitData> hitReason = makeTypedReasonKind<ReasonKind::InvariantPrefixHit>();
    static constexpr InvariantLetter invalidLetter = InvariantLetter::invalid();

    static size_t hashLetter(InvariantLetter letter) { return std::bit_cast<uint32_t>(letter.payload); }
};

//! The prefix index of invariant sets
struct InvariantPrefixes : PrefixIndex<InvariantPrefixes> {
    explicit InvariantPrefixes(Solver&) { }

    Value watchedValue(InvariantWord word) const { return word.member; }
    void appendLetters(Solver&, InvariantWord, std::vector<InvariantLetter>& out);
    void explainLetters(Solver&, InvariantWord, ClauseBuilder&);

private:
    // Temporary buffer used inside a single function to avoid repeated allocations
    std::vector<Member> memberBuffer;
};

}
