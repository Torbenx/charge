#pragma once

#include <verify/backend/Data.h>
#include <verify/backend/SatCore.h>
#include <verify/backend/Sets.h>
#include <verify/backend/Trace.h>

#include <unordered_map>

namespace verify::backend {

//! Detects prefix relations between two sets of member expressions
/*!
The structure maintains a set A of paths and a set B of prefix candidates separately for any number
of elements. The data structure detects a prefix candidate is a prefix of a path for the same
element and than creates a conflict justified with the MemberPrefixHit reason. The prefix detection
dynamically updates as rewrites are applied.

The word are stored in a trie with a separate root node for each element. Note that the trie is
never trimmed, so it contains all distinct normal forms the search has ever visited.
*/
struct MemberPrefixes {
    using ElementId = Sets::ElementId;

    struct WordId {
        explicit WordId(uint32_t id)
            : m_id(id) { }
        uint32_t id() const { return m_id; }

        bool operator==(const WordId&) const = default;

    private:
        uint32_t m_id;
    };

    explicit MemberPrefixes(Solver&);

    //! Add \p expression to the set A or B of \p element
    /*!
    When containment.contained() is true this is path otherwise it is a prefix candiate.
    */
    WordId addWord(Solver&, Member expression, ElementId, Sets::Containment);

    ElementId elementOf(WordId w) const { return words[w].element; }
    Sets::Containment containmentOf(WordId w) const { return words[w].containment; }

    //! Returns whether \p prefix is currently a prefix of \p path
    /*!
    Both words must belong to the same element.
    */
    bool isPrefixOf(WordId prefix, WordId path) const;
    void explainPrefix(Solver&, WordId prefix, WordId path, ClauseBuilder& clause);

    //! Bring the word \p w up to date with the rewrites applied by the Members theory
    /*!
    This is the notification of the use registered for the word by addWord(), so Members only calls
    it when the normal form of the word really changed. Reverting a rewrite is not notified about,
    see \ref rebuilds.
    */
    void propagateRewrite(Solver&, Use);

    void newDecisionLevel(Solver&);
    //! Unregister the words of the reverted levels and rebuild the ones the backtrack changed
    /*!
    The unregistered words are kept around until endBacktrack() so that a reason naming them can
    still be turned into a clause while the assignments are being justified.
    */
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

    //! Explicitly check that the invariances of the structure hold
    void checkInvariances(Solver&);

private:
    struct Node {
        Member letter; //!< The last letter of the word spelled by this node
        ElementId element;

        //! The paths of the set A whose normal form has the word of this node as a prefix
        std::vector<WordId> aOccurrences = {};
        //! The prefix candidates of the set B whose normal form is the word of this node
        std::vector<WordId> bTerminals = {};

        bool conflict() const { return !aOccurrences.empty() && !bTerminals.empty(); }
    };

    struct WordInfo {
        Member expression; //!< The expression as it was registered
        ElementId element;
        Sets::Containment containment;
        bool isPath() const { return containment.contained(); };

        //! The trie nodes for every prefix of the normal form, starting with the element root
        /*!
        Therefore path.size() is the length of the normal form plus one and path.back() is the
        terminal node of the word.
        */
        std::vector<uint32_t> path = {};

        //! Indices into Node::aOccurrences or Node::bTerminals
        /*!
        For a path this has one entry per node in path, for a prefix candidate it has a single
        entry for the terminal node.
        */
        std::vector<uint32_t> occurrenceIndices = {};
    };

    uint32_t childNode(uint32_t parent, Member letter);

    void buildPath(Solver&, WordId);
    void attach(Solver&, WordId, bool raiseConflicts);
    void detach(WordId);

    Node& nodeOf(uint32_t node) { return nodes[node]; }

    //! The edge from a node to the child reached by appending a letter
    struct Edge {
        uint32_t parent;
        Member letter;

        bool operator==(const Edge&) const = default;
    };

    struct EdgeHash {
        size_t operator()(const Edge& edge) const {
            size_t hash = 0;
            hash_combine(hash, edge.parent);
            hash_combine(hash, std::bit_cast<uint32_t>((Value)edge.letter));
            return hash;
        }
    };

    std::vector<Node> nodes; //!< Append-only, never unwound
    std::unordered_map<Edge, uint32_t, EdgeHash> edges; //!< Append-only, never unwound
    std::vector<uint32_t> elementRoots;

    Trace<WordInfo, WordId> words;
    Trace<WordId> rewriteTrace;
    //! The number of words that survive the backtrack in progress, see beginBacktrack()
    uint32_t backtrackedWordCount = limits::max;

    bool backtracking() const { return backtrackedWordCount != (uint32_t)limits::max; }
    bool isBacktracked(WordId word) const { return word.id() >= backtrackedWordCount; }

    // Temporary buffer used inside a single function to avoid repeated allocations
    std::vector<Member> normalFormBuffer;
};

struct MemberPrefixHitData {
    MemberPrefixes::WordId prefix;
    MemberPrefixes::WordId path;
};

}
