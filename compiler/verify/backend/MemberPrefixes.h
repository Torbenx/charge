#pragma once

#include <verify/backend/Data.h>
#include <verify/backend/SatCore.h>
#include <verify/backend/Sets.h>

#include <unordered_map>

namespace verify::backend {

//! Detects prefix relations between two sets of member expressions
/*!
The structure maintains any number of independent elements. Each element owns two sets of member
expressions:
- The paths, forming the set A
- The prefix candidates, forming the set B
and detects whether the normal form of some prefix candidate is a prefix of the normal form of some
path, where the normal form is the expression after applying all rewrites currently known to the
Members theory.

Such a relation is taken to be a contradiction, so it is reported by assigning false_literal with a
MemberPrefixHit reason. The owner of the structure defines that reason and completes the clause
with its own justification for the two expressions being in their sets, see explainHit().

Detection is sound but not complete with respect to entailment: a prefix relation is reported once
the currently applied rewrites make it syntactically visible. A pair that could still become a
prefix under further equalities is not reported.

\section representation Representation

All words are stored in a single trie over the letters of their normal forms. Each element owns a
root node, so elements are kept apart by construction and a node always belongs to exactly one
element. A node is identified with the word spelled by the path from its elements root, which is
what makes the structure work:
- Two words have the same normal form exactly when they end in the same node.
- A word b is a prefix of a word a exactly when a's path passes through b's terminal node, which is
  an O(1) test once both paths are known.

The trie is a pure function of the words it has seen, so it is append-only and is never unwound.
Only the annotations recording which words currently sit where are backtracked. Nodes therefore
survive backtracking as a cache and are reused when the search revisits similar assignments.

Note that this cache is never trimmed, so the trie grows with the number of distinct normal forms
the search has ever visited and only the annotations shrink again. That is the price of not having
to rebuild a node the search comes back to.

\section monotonicity Monotonicity

Applying a rewrite is applying a substitution, and u being a prefix of v implies that sigma(u) is a
prefix of sigma(v) for every substitution sigma. Hence rewriting can only create prefix relations
and backtracking can only destroy them. The structure relies on this: it never has to rediscover a
relation while unwinding, which is what SatCore requires of a theory during a backtrack.
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
    A prefix relation found between the two sets is an immediate conflict, so this assigns
    false_literal with a MemberPrefixHit reason as soon as one is detected. The owner of the
    structure is expected to turn that reason into a clause, using \p payload to recover whatever it
    needs to explain why the expression is in the set.
    When payload.contained() is true this is path otherwise it is a prefix candiate.
    */
    WordId addWord(Solver&, ElementId, Member expression, Sets::Containment payload);

    ElementId elementOf(WordId w) const { return words[w.id()].element; }
    Sets::Containment payloadOf(WordId w) const { return words[w.id()].payload; }

    //! Returns whether \p prefix is currently a prefix of \p path
    /*!
    Both words must belong to the same element.
    */
    bool isPrefixOf(WordId prefix, WordId path) const;
    void explainPrefix(Solver&, WordId prefix, WordId path, ClauseBuilder& clause);

    //! Bring the index up to date with the rewrites applied by the Members theory
    /*!
    Must be called whenever Members has reached a quiescent state, i.e. after each grind().
    */
    void propagateRewrites(Solver& solver) { updateRewrites(solver, true); }

    void newDecisionLevel(Solver&);
    //! Unregister the words of the reverted levels and bring the remaining ones up to date
    /*!
    The unregistered words are kept around until endBacktrack() so that a reason naming them can
    still be turned into a clause while the assignments are being justified.
    */
    void beginBacktrack(Solver&);
    void endBacktrack(Solver&);

    //! Explicitly check that the invariances of the structure hold
    void checkInvariances(Solver&);

private:
    static constexpr uint32_t NIL = limits::max;

    struct Node {
        uint32_t parent; //!< NIL for the root of an element
        Member letter; //!< The last letter of the word spelled by this node
        ElementId element;
        uint32_t depth; //!< Length of the word spelled by this node

        //! The paths of the set A whose normal form has the word of this node as a prefix
        std::vector<WordId> aOccurrences = {};
        //! The prefix candidates of the set B whose normal form is the word of this node
        std::vector<WordId> bTerminals = {};

        bool conflict() const { return !aOccurrences.empty() && !bTerminals.empty(); }
    };

    struct WordInfo {
        ElementId element;
        Member expression; //!< The expression as it was registered, never rewritten in place
        Sets::Containment payload; //!< Opaque data of the owner, see addPath()
        bool isPath() const { return payload.contained(); };
        bool dirty = false;

        //! The trie nodes for every prefix of the normal form, starting with the element root
        /*!
        Therefore path.size() is the length of the normal form plus one and path.back() is the
        terminal node of the word.
        */
        std::vector<uint32_t> path = {};

        //! Back indices into Node::aOccurrences for a path, or into Node::bTerminals for a
        //! prefix candidate
        /*!
        For a path this has one entry per node in path, for a prefix candidate it has a single
        entry for the terminal node.
        */
        std::vector<uint32_t> occurrenceIndices = {};

        //! The distinct variables occurring in \ref expression, sorted
        /*!
        The normal form can only change when one of these gets a new rewrite, because Members marks
        a variable as changed whenever the expansion of any variable it depends on changes.
        */
        std::vector<Member> watchedVariables = {};
    };

    uint32_t childNode(uint32_t parent, Member letter);

    void collectVariables(Solver&, Member expression, std::vector<Member>& out);

    void updateRewrites(Solver&, bool raiseConflicts);
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

    std::vector<WordInfo> words;
    std::vector<uint32_t> wordDecisionPoints;
    size_t backtrackedWordCount = limits::max;

    bool backtracking() const { return backtrackedWordCount != (decltype(backtrackedWordCount))limits::max; }
    bool isBacktracked(WordId word) const { return (size_t)word.id() >= backtrackedWordCount; }

    SortData<std::vector<WordId>, Sort::Member> variableUses;

    // Temporary buffers used inside single function to avoid repeated allocations
    std::vector<WordId> dirtyWords;
    std::vector<Member> normalFormBuffer;
};

struct MemberPrefixHitData {
    MemberPrefixes::WordId prefix;
    MemberPrefixes::WordId path;
};

}
