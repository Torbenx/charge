#pragma once

#include <verify/backend/Value.h>

namespace verify::backend {

//! A member expression prepared for the satisfiability tests
struct CompiledMemberString {
    using mask_t = uint64_t;

    struct Letter {
        Member member;
        uint32_t position;
    };

    static mask_t positionMask(int_t pos) { return (mask_t)1 << pos; }

    CompiledMemberString(std::span<const Member> expression);

    bool canBeEmpty() const;
    bool canBeEqual(Member letter) const;
    bool canBeEqual(const CompiledMemberString& b) const;

    int_t size() const { return letters.size(); }
    mask_t sizeMask() const { return ((mask_t)1 << size()) - (mask_t)1; }

    //! Letters sorted by member
    std::vector<Letter> letters;
    //! Contains 1 at the positions that are literals
    mask_t literalMask = 0;
    //! Contains 1 at the positions of the variables occurring more than once
    mask_t duplicateVariableMask = 0;
};

}