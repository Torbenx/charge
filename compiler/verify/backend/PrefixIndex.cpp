#include <verify/backend/PrefixIndex.h>

#include <verify/backend/SolverImpl.h>

namespace verify::backend {

PrefixIndex::WordId PrefixIndex::addWord(Solver& solver, Member expression, ElementId element, Sets::Containment containment, Role role, SelfInclusion inclusion) {
    while (element.id() >= elementRoots.size()) {
        uint32_t root = nodes.size();
        nodes.push_back({ .letter = (Member)INVALID_VALUE, .element = ElementId(elementRoots.size()) });
        elementRoots.push_back(root);
    }

    WordId w = words.push({ .expression = expression, .element = element, .containment = containment, .role = role, .selfInclusion = inclusion });
    solver.addUse(expression, Use(params().wordUse, w.id()));

    buildPath(solver, w);
    attach(solver, w, true);
    return w;
}

bool PrefixIndex::isPrefixOf(WordId prefix, WordId path) const {
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

bool PrefixIndex::raisesConflict(WordId prefix, WordId path) const {
    const WordInfo& prefixInfo = words[prefix];
    const WordInfo& pathInfo = words[path];

    // Two negative set containments can never cause a conflict (the element being in no set at all
    // is always a valid assignment). More pactically across all the prefix/path matches of invariant
    // sets the negative/negative containment cases are the only ones that are not a conflict
    // (see InvariantSetsPrefixCases.md). So not raising such conflicts here allows stuffing all of
    // these into one PrefixIndex.
    if (!prefixInfo.containment.contained() && !pathInfo.containment.contained())
        return false;

    // Require strict prefix when matching an exlusive prefix against and inclusive path.
    if (prefixInfo.selfInclusion == SelfInclusion::Exclusive && pathInfo.selfInclusion == SelfInclusion::Inclusive)
        return nodes[pathInfo.path.back()].literalCount > nodes[prefixInfo.path.back()].literalCount;

    return true;
}

bool PrefixIndex::isConflict(WordId prefix, WordId path) const {
    return isPrefixOf(prefix, path) && raisesConflict(prefix, path);
}

void PrefixIndex::explainPrefix(Solver& solver, WordId prefix, WordId path, ClauseBuilder& clause) {
    // The prefix relation follows from the spellings of the two words alone, so whatever those
    // are derived from is all that has to be justified.
    // TODO: Only the letters covering the matched prefix of the path are needed, explaining the
    //       whole word is sound but produces a longer clause than necessary.
    solver.impl().members.explainRewrite(solver, words[prefix].expression, clause);
    solver.impl().members.explainRewrite(solver, words[path].expression, clause);
}

void PrefixIndex::propagateRewrite(Solver& solver, Use use) {
    // Only growing rewrites are notified about, so this never runs while the assignments are
    // being reverted and a conflict found here is a real one
    VERIFY(!backtracking());
    VERIFY(use.kind() == params().wordUse);
    WordId w = WordId(use.id());

    rewriteTrace.push(w);
    detach(w);
    buildPath(solver, w);
    attach(solver, w, true);
}

void PrefixIndex::newDecisionLevel(Solver& solver) {
    words.newDecisionLevel(solver);
    rewriteTrace.newDecisionLevel(solver);
}

void PrefixIndex::beginBacktrack(Solver& solver) {
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

void PrefixIndex::endBacktrack(Solver& solver) {
    VERIFY(backtrackedWordCount == words.backtrackedBegin(solver).id());
    backtrackedWordCount = limits::max;
    words.truncate(solver);
}

void PrefixIndex::checkInvariances(Solver& solver) {
    VERIFY(!backtracking());
    words.checkInvariances(solver);
    rewriteTrace.checkInvariances(solver);

    for (WordId w : rewriteTrace)
        VERIFY(w.id() < words.size());

    for (int_t node = 0; node < (int_t)nodes.size(); node++) {
        const Node& n = nodes[node];

        for (int_t i = 0; i < (int_t)n.aOccurrences.size(); i++) {
            const WordInfo& info = words[n.aOccurrences[i]];
            VERIFY(info.isPath());
            auto it = std::ranges::find(info.path, node);
            VERIFY(it != info.path.end());
            VERIFY(info.occurrenceIndices[it - info.path.begin()] == i);
        }
        for (int_t i = 0; i < (int_t)n.bTerminals.size(); i++) {
            const WordInfo& info = words[n.bTerminals[i]];
            VERIFY(!info.isPath());
            VERIFY(info.path.back() == node);
            VERIFY(info.occurrenceIndices.size() == 1);
            VERIFY(info.occurrenceIndices[0] == i);
        }
    }

    std::vector<Member> letters;
    for (const WordInfo& info : words) {
        // The path spells the word, so anything else here means that a notification of the use
        // registered for it was missed
        letters.clear();
        solver.impl().members.appendRewrite(info.expression, letters);
        VERIFY(info.path.size() == letters.size() + 1);
        VERIFY(info.path.front() == elementRoots[info.element.id()]);
        int_t literalCount = 0;
        for (int_t k = 0; k < (int_t)letters.size(); k++) {
            VERIFY(nodes[info.path[k + 1]].letter == letters[k]);
            if (letters[k].literal())
                literalCount += 1;
            VERIFY(nodes[info.path[k + 1]].literalCount == literalCount);
        }
    }
}

uint32_t PrefixIndex::childNode(uint32_t parent, Member letter) {
    auto [it, inserted] = edges.try_emplace(Edge { parent, letter }, (uint32_t)nodes.size());
    if (inserted) {
        const Node& parentNode = nodes[parent];
        nodes.push_back({ .letter = letter,
            .element = parentNode.element,
            .literalCount = parentNode.literalCount + (letter.literal() ? 1u : 0u) });
    }
    return it->second;
}

void PrefixIndex::buildPath(Solver& solver, WordId w) {
    WordInfo& info = words[w];
    VERIFY(info.path.empty());

    normalFormBuffer.clear();
    solver.impl().members.appendRewrite(info.expression, normalFormBuffer);

    uint32_t node = elementRoots[info.element.id()];
    info.path.push_back(node);
    for (Member letter : normalFormBuffer) {
        node = childNode(node, letter);
        info.path.push_back(node);
    }
}

void PrefixIndex::attach(Solver& solver, WordId w, bool raiseConflicts) {
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
                for (WordId prefix : n.bTerminals) {
                    if (raisesConflict(prefix, w))
                        solver.assignTrue(false_literal, makeReason(params().hitReason, { .prefix = prefix, .path = w }));
                }
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
            for (WordId path : n.aOccurrences) {
                if (raisesConflict(w, path))
                    solver.assignTrue(false_literal, makeReason(params().hitReason, { .prefix = w, .path = path }));
            }
        }
    }
}

void PrefixIndex::detach(WordId w) {
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

}