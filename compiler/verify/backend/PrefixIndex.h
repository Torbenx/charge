#pragma once

#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>

#include <algorithm>
#include <unordered_map>

namespace verify::backend {

//! Detects prefix relations between two sets of words
/*!
The structure maintains a set A of paths and a set B of prefix candidates separately for any number
of elements. The data structure detects a prefix candidate is a prefix of a path for the same
element and than creates a conflict justified with the reason of \ref prefix_index_Impl::hitReason.
The prefix detection dynamically updates as rewrites are applied.

The words are stored in a trie with a separate root node for each element. Note that the trie is
never trimmed, so it contains all distinct spellings the search has ever visited.

The impl has to provide the following static information:
- \c Letter: the letters the words are spelled with
- \c WordKey: the description a word is built from, small enough to be passed by value
- \c wordUse: the UseKind of the rewrite notifications of the words
- \c hitReason: the TypedReasonKind<PrefixHitData> of the conflicts raised for a hit
- \c invalidLetter: a letter that never occurs in a word, used for the roots
- \c hashLetter(Letter): the hash of a letter
- \c letterStable(Letter): whether a later rewrite can remove this letter from its word

And the following member functions:
- \c Value \c watchedValue(WordKey): the value whose rewrites the word follows
- \c void \c appendLetters(Solver&, WordKey, std::vector<Letter>&): the current spelling of the word
- \c void \c explainLetters(Solver&, WordKey, ClauseBuilder&): justify that spelling
- \c bool \c raisesConflict(Hit prefix, Hit path, bool strictPrefix) const: whether a hit is a conflict
*/
struct PrefixIndex {
    struct Params {
        TypedReasonKind<PrefixHitData> hitReason;
        UseKind wordUse;
    };

    struct WordId {
        explicit WordId(uint32_t id)
            : m_id(id) { }
        uint32_t id() const { return m_id; }

        bool operator==(const WordId&) const = default;

    private:
        uint32_t m_id;
    };

    enum class Role : uint8_t {
        Prefix,
        Path,
    };

    enum class SelfInclusion : uint8_t {
        Inclusive,
        Exclusive,
    };

    PrefixIndex(const Params& params)
        : m_params(params) { }

    //! Add the word described by \p key to the set of \p element selected by \p role
    WordId addWord(Solver&, Member expression, SetElement element, SetContainment containment, Role role, SelfInclusion inclusion);

    Member expressionOf(WordId w) { return words[w].expression; }
    SetElement elementOf(WordId w) const { return words[w].element; }
    SetContainment containmentOf(WordId w) const { return words[w].containment; }

    //! Returns whether \p prefix is currently a non-strict prefix of \p path
    /*!
    Both words must belong to the same element.
    */
    bool isPrefixOf(WordId prefix, WordId path) const;

    //! Returns whether \p prefix and \p path are currently a conflict
    /*!
    Combines isPrefixOf() and raisesConflict(). Both words must belong to the same element.
    */
    bool isConflict(WordId prefix, WordId path) const;

    void explainPrefix(Solver&, WordId prefix, WordId path, ClauseBuilder& clause);

    //! Bring the word \p w up to date with the rewrites applied by the theory it follows
    void propagateRewrite(Solver&, Use);

    void newDecisionLevel(Solver&);
    //! Unregister the words of the reverted levels and rebuild the ones the backtrack changed
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

    const Params& params() const { return m_params; }

    //! Explicitly check that the invariances of the structure hold
    void checkInvariances(Solver&);

private:
    struct Node {
        Member letter; //!< The last letter of the word spelled by this node
        SetElement element;
        //! The number of literals in the word of this node
        /*!
        Used to implement strict prefix detection in O(1).
        */
        uint32_t literalCount = 0;

        //! The paths of the set A whose spelling has the word of this node as a prefix
        std::vector<WordId> aOccurrences = {};
        //! The prefix candidates of the set B whose spelling is the word of this node
        std::vector<WordId> bTerminals = {};

        bool conflict() const { return !aOccurrences.empty() && !bTerminals.empty(); }
    };

    struct WordInfo {
        Member expression; //!< The description the word is spelled from
        SetElement element;
        SetContainment containment;
        Role role;
        SelfInclusion selfInclusion;
        bool isPath() const { return role == Role::Path; };

        //! The trie nodes for every prefix of the spelling, starting with the element root
        /*!
        Therefore path.size() is the length of the spelling plus one and path.back() is the
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
            hash_combine(hash, std::bit_cast<uint32_t>(edge.letter));
            return hash;
        }
    };

    //! Whether a hit between \p prefix and \p path is a conflict
    bool raisesConflict(WordId prefix, WordId path) const;

    uint32_t childNode(uint32_t parent, Member letter);

    void buildPath(Solver&, WordId);
    void attach(Solver&, WordId, bool raiseConflicts);
    void detach(WordId);

    Node& nodeOf(uint32_t node) { return nodes[node]; }

    Params m_params;

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

struct PrefixHitData {
    PrefixIndex::WordId prefix;
    PrefixIndex::WordId path;
};

}
