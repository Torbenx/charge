#pragma once

#include <verify/backend/Sets.h>
#include <verify/backend/Solver.h>
#include <verify/backend/Trace.h>

#include <algorithm>
#include <unordered_map>

namespace verify::backend {

struct PrefixIndexWordId {
    explicit PrefixIndexWordId(uint32_t id)
        : m_id(id) { }
    uint32_t id() const { return m_id; }

    bool operator==(const PrefixIndexWordId&) const = default;

private:
    uint32_t m_id;
};

struct PrefixHitData {
    PrefixIndexWordId prefix;
    PrefixIndexWordId path;
};

//! Specialized by every user of PrefixIndex
/*!
The specialization has to provide:
- \c Letter: the letters the words are spelled with
- \c WordKey: the description a word is built from, small enough to be passed by value
- \c wordUse: the UseKind of the rewrite notifications of the words
- \c hitReason: the TypedReasonKind<PrefixHitData> of the conflicts raised for a hit
- \c invalidLetter: a letter that never occurs in a word, used for the roots
- \c hashLetter(Letter): the hash of a letter
*/
template<typename Derived>
struct prefix_index_traits;

//! Detects prefix relations between two sets of words
/*!
The structure maintains a set A of paths and a set B of prefix candidates separately for any number
of elements. The data structure detects a prefix candidate is a prefix of a path for the same
element and than creates a conflict justified with the reason of \ref prefix_index_traits::hitReason.
The prefix detection dynamically updates as rewrites are applied.

The words are stored in a trie with a separate root node for each element. Note that the trie is
never trimmed, so it contains all distinct spellings the search has ever visited.

The words are spelled by the derived structure, which has to provide:
- \c Value \c watchedValue(WordKey): the value whose rewrites the word follows
- \c void \c appendLetters(Solver&, WordKey, std::vector<Letter>&): the current spelling of the word
- \c void \c explainLetters(Solver&, WordKey, ClauseBuilder&): justify that spelling
*/
template<typename Derived>
struct PrefixIndex {
    using Traits = prefix_index_traits<Derived>;
    using Letter = typename Traits::Letter;
    using WordKey = typename Traits::WordKey;

    using WordId = PrefixIndexWordId;
    using ElementId = Sets::ElementId;

    //! Add the word described by \p key to the set A or B of \p element
    /*!
    When containment.contained() is true this is path otherwise it is a prefix candiate.
    */
    WordId addWord(Solver& solver, WordKey key, ElementId element, Sets::Containment containment) {
        while (element.id() >= elementRoots.size()) {
            uint32_t root = nodes.size();
            nodes.push_back({ .letter = Traits::invalidLetter, .element = ElementId(elementRoots.size()) });
            elementRoots.push_back(root);
        }

        WordId w = words.push({ .key = key, .element = element, .containment = containment });
        solver.addUse(derived().watchedValue(key), Use(Traits::wordUse, w.id()));

        buildPath(solver, w);
        attach(solver, w, true);
        return w;
    }

    WordKey keyOf(WordId w) const { return words[w].key; }
    ElementId elementOf(WordId w) const { return words[w].element; }
    Sets::Containment containmentOf(WordId w) const { return words[w].containment; }

    //! Returns whether \p prefix is currently a prefix of \p path
    /*!
    Both words must belong to the same element.
    */
    bool isPrefixOf(WordId prefix, WordId path) const {
        if (isBacktracked(prefix) || isBacktracked(path))
            return false;
        const WordInfo& prefixInfo = words[prefix];
        const WordInfo& pathInfo = words[path];
        VERIFY(prefixInfo.element == pathInfo.element);
        // Because a node is identified with the word it spells, comparing the single node at the
        // length of the prefix decides the whole prefix relation.
        if (pathInfo.path.size() < prefixInfo.path.size())
            return false;
        return pathInfo.path[prefixInfo.path.size() - 1] == prefixInfo.path.back();
    }

    void explainPrefix(Solver& solver, WordId prefix, WordId path, ClauseBuilder& clause) {
        // The prefix relation follows from the spellings of the two words alone, so whatever those
        // are derived from is all that has to be justified.
        // TODO: Only the letters covering the matched prefix of the path are needed, explaining the
        //       whole word is sound but produces a longer clause than necessary.
        derived().explainLetters(solver, words[prefix].key, clause);
        derived().explainLetters(solver, words[path].key, clause);
    }

    //! Bring the word \p w up to date with the rewrites applied by the theory it follows
    void propagateRewrite(Solver& solver, Use use) {
        // Only growing rewrites are notified about, so this never runs while the assignments are
        // being reverted and a conflict found here is a real one
        VERIFY(!backtracking());
        VERIFY(use.kind() == Traits::wordUse);
        WordId w = WordId(use.id());

        rewriteTrace.push(w);
        detach(w);
        buildPath(solver, w);
        attach(solver, w, true);
    }

    void newDecisionLevel(Solver& solver) {
        words.newDecisionLevel(solver);
        rewriteTrace.newDecisionLevel(solver);
    }

    //! Unregister the words of the reverted levels and rebuild the ones the backtrack changed
    void beginBacktrack(Solver& solver) {
        backtrackedWordCount = words.backtrackedBegin(solver).id();

        for (WordId w : words.backtrackedPositionsReverse(solver))
            detach(w);

        for (WordId w : rewriteTrace.backtrackedReverse(solver)) {
            // The word was unregistered above, its spelling no longer matters
            if (isBacktracked(w))
                continue;

            detach(w);
            buildPath(solver, w);
            // Reverting a rewrite can only destroy prefix relations, so there is nothing to raise here.
            attach(solver, w, false);
        }
        rewriteTrace.truncate(solver);
    }

    void endBacktrack(Solver& solver) {
        VERIFY(backtrackedWordCount == words.backtrackedBegin(solver).id());
        backtrackedWordCount = limits::max;
        words.truncate(solver);
    }

    //! Explicitly check that the invariances of the structure hold
    void checkInvariances(Solver& solver) {
        VERIFY(!backtracking());
        words.checkInvariances(solver);
        rewriteTrace.checkInvariances(solver);

        for (WordId w : rewriteTrace)
            VERIFY(w.id() < words.size());

        for (uint32_t node = 0; node < nodes.size(); node++) {
            const Node& n = nodes[node];

            for (uint32_t i = 0; i < n.aOccurrences.size(); i++) {
                const WordInfo& info = words[n.aOccurrences[i]];
                VERIFY(info.isPath());
                auto it = std::ranges::find(info.path, node);
                VERIFY(it != info.path.end());
                VERIFY(info.occurrenceIndices[it - info.path.begin()] == i);
            }
            for (uint32_t i = 0; i < n.bTerminals.size(); i++) {
                const WordInfo& info = words[n.bTerminals[i]];
                VERIFY(!info.isPath());
                VERIFY(info.path.back() == node);
                VERIFY(info.occurrenceIndices.size() == 1);
                VERIFY(info.occurrenceIndices[0] == i);
            }
        }

        std::vector<Letter> letters;
        for (const WordInfo& info : words) {
            // The path spells the word, so anything else here means that a notification of the use
            // registered for it was missed
            letters.clear();
            derived().appendLetters(solver, info.key, letters);
            VERIFY(info.path.size() == letters.size() + 1);
            VERIFY(info.path.front() == elementRoots[info.element.id()]);
            for (uint32_t k = 0; k < letters.size(); k++) {
                VERIFY(nodes[info.path[k + 1]].letter == letters[k]);
            }
        }
    }

private:
    struct Node {
        Letter letter; //!< The last letter of the word spelled by this node
        ElementId element;

        //! The paths of the set A whose spelling has the word of this node as a prefix
        std::vector<WordId> aOccurrences = {};
        //! The prefix candidates of the set B whose spelling is the word of this node
        std::vector<WordId> bTerminals = {};

        bool conflict() const { return !aOccurrences.empty() && !bTerminals.empty(); }
    };

    struct WordInfo {
        WordKey key; //!< The description the word is spelled from
        ElementId element;
        Sets::Containment containment;
        bool isPath() const { return containment.contained(); };

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
        Letter letter;

        bool operator==(const Edge&) const = default;
    };

    struct EdgeHash {
        size_t operator()(const Edge& edge) const {
            size_t hash = 0;
            hash_combine(hash, edge.parent);
            hash_combine(hash, Traits::hashLetter(edge.letter));
            return hash;
        }
    };

    Derived& derived() { return static_cast<Derived&>(*this); }

    uint32_t childNode(uint32_t parent, Letter letter) {
        auto [it, inserted] = edges.try_emplace(Edge { parent, letter }, (uint32_t)nodes.size());
        if (inserted) {
            const Node& parentNode = nodes[parent];
            nodes.push_back({ .letter = letter, .element = parentNode.element });
        }
        return it->second;
    }

    void buildPath(Solver& solver, WordId w) {
        WordInfo& info = words[w];
        VERIFY(info.path.empty());

        letterBuffer.clear();
        derived().appendLetters(solver, info.key, letterBuffer);

        uint32_t node = elementRoots[info.element.id()];
        info.path.push_back(node);
        for (Letter letter : letterBuffer) {
            node = childNode(node, letter);
            info.path.push_back(node);
        }
    }

    void attach(Solver& solver, WordId w, bool raiseConflicts) {
        WordInfo& info = words[w];
        VERIFY(info.occurrenceIndices.empty());

        if (info.isPath()) {
            // A path occupies every prefix of its spelling, so that a prefix candidate ending in any
            // of those nodes is detected there, and so that a witness is available at every node.
            info.occurrenceIndices.reserve(info.path.size());
            for (uint32_t node : info.path) {
                Node& n = nodeOf(node);
                VERIFY(n.element == info.element);
                info.occurrenceIndices.push_back(n.aOccurrences.size());
                n.aOccurrences.push_back(w);
                if (raiseConflicts) {
                    // Every match is a separate conflict
                    for (WordId prefix : n.bTerminals)
                        solver.assignTrue(false_literal, makeReason(Traits::hitReason, { .prefix = prefix, .path = w }));
                }
            }
        } else {
            uint32_t node = info.path.back();
            Node& n = nodeOf(node);
            VERIFY(n.element == info.element);
            info.occurrenceIndices.push_back(n.bTerminals.size());
            n.bTerminals.push_back(w);
            if (raiseConflicts) {
                // Every match is a separate conflict
                for (WordId path : n.aOccurrences)
                    solver.assignTrue(false_literal, makeReason(Traits::hitReason, { .prefix = w, .path = path }));
            }
        }
    }

    void detach(WordId w) {
        WordInfo& info = words[w];

        auto removeAt = [&](std::vector<WordId>& list, uint32_t index, uint32_t indexSlot) {
            VERIFY(list[index] == w);
            if (index + 1 != list.size()) {
                list[index] = list.back();
                words[list[index]].occurrenceIndices[indexSlot] = index;
            }
            list.pop_back();
        };

        if (info.isPath()) {
            VERIFY(info.occurrenceIndices.size() == info.path.size());
            for (int_t k = info.path.size() - 1; k >= 0; k--) {
                removeAt(nodeOf(info.path[k]).aOccurrences, info.occurrenceIndices[k], k);
            }
        } else {
            VERIFY(info.occurrenceIndices.size() == 1);
            uint32_t node = info.path.back();
            removeAt(nodeOf(node).bTerminals, info.occurrenceIndices[0], 0);
        }

        info.occurrenceIndices.clear();
        info.path.clear();
    }

    Node& nodeOf(uint32_t node) { return nodes[node]; }

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
    std::vector<Letter> letterBuffer;
};

}
