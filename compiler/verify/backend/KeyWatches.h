#pragma once

#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>
#include <verify/backend/Use.h>

#include <algorithm>

namespace verify::backend {

struct KeyWatchesParams {
    UseKind keyUse;
    UseKind watchUse;
};

//! Detects when the watched values of an element match its key
/*!
For each element the structure maintains a single key and any number of watches. Whenever a watch
matches the key of its element onKeyMatch() is called for it, exactly once.

The derived class has to provide the following static information:
- \c PARAMS: the KeyWatchesParams of the structure

And the following member functions:
- \c void \c onKeyMatch(Solver&, ElementId, KeyType key, WatchType watch): the match callback

It may also replace the two hooks:
- \c bool \c matches(Solver&, ElementId, KeyType key, WatchType watch): whether the two currently match
- \c void \c addValueUses(Solver&, ElementId, KeyType | WatchType, Use): register the rewrite notifications of a value

Keys, watches and the matches reported for them are all reverted by beginBacktrack().
*/
template<typename Derived, typename KeyType = Value, typename WatchType = Value>
struct KeyWatches {
    using Params = KeyWatchesParams;
    using ElementId = Sets::ElementId;

    struct WatchId {
        explicit WatchId(uint32_t id)
            : m_id(id) { }
        uint32_t id() const { return m_id; }

        auto operator<=>(const WatchId&) const = default;
        bool operator==(const WatchId&) const = default;

    private:
        uint32_t m_id;
    };

    std::optional<KeyType> keyOf(ElementId) const;
    void setKey(Solver&, ElementId, KeyType key);
    void addWatch(Solver&, ElementId, WatchType watch);

    void propagateRewrite(Solver&, Use);

    void newDecisionLevel(Solver&);
    void beginBacktrack(Solver&);

    //! Whether \p watch matches \p key, replaceable by the derived class
    bool matches(Solver&, ElementId, KeyType key, WatchType watch);
    //! Register \p use for the values a match of \p watch depends on, replaceable by the derived class
    void addValueUses(Solver&, ElementId, WatchType watch, Use);

    void checkInvariances(Solver&);

    static constexpr Params params() { return Derived::PARAMS; }

private:
    struct WatchEntry {
        ElementId element;
        WatchType value;
        //! Becomes true when the watch is found to match the key
        bool matched = false;
    };

    struct ElementState {
        std::optional<KeyType> key;
        //! The watches of this element, in increasing order
        std::vector<WatchId> watchIds;
    };

    Derived& derived();
    ElementState& stateOf(ElementId element);

    void reportMatch(Solver&, ElementId, WatchId watch);
    void reportMatchesOf(Solver&, ElementId);

    std::vector<ElementState> elementStates;
    //! The watches, referenced by ElementState::watchIds
    Trace<WatchEntry, WatchId> watches;
    Trace<ElementId> keyTrace;
    Trace<WatchId> matchTrace;
};

}
