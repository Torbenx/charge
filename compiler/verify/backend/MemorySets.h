#pragma once

#include <verify/backend/MemoryLocationSets.h>

namespace verify::backend {

struct MemorySets : MemoryLocationSets<MemorySets> {
    static constexpr Params PARAMS = {
        .setSort = Sort::MemorySet,
        .declarationsShareElementReason = makeTypedReasonKind<ReasonKind::MemoryDeclarationsShareElement>(),
        .prefixParams = {
            .hitReason = makeTypedReasonKind<ReasonKind::MemberPrefixHit>(),
            .wordUse = UseKind::MemoryPrefixWord,
        },
        .watchesParams = {
            .keyUse = UseKind::MemorySetRepresentative,
            .watchUse = UseKind::MemorySetPendingContainment,
        },
    };
    using Base = MemoryLocationSets<MemorySets>;
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
    void addWords(Solver& solver, PrefixIndex& prefixes, SetElement element, SetContainment cont) {
        // If an element is in a location set but not in the location set of some prefix of it, thats a conflict.
        auto role = cont.contained() ? PrefixIndex::Role::Path : PrefixIndex::Role::Prefix;
        prefixes.addWord(solver, locationOf((MemorySet)cont.set()).member, element, cont, role, PrefixIndex::SelfInclusion::Inclusive);
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