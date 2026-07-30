#include <verify/backend/MemberPrefixes.h>

#include <verify/backend/SolverImpl.h>

#include <algorithm>

namespace verify::backend {

MemberPrefixes::MemberPrefixes(Solver&) { }

uint32_t MemberPrefixes::childNode(uint32_t parent, Member letter) {
    auto [it, inserted] = edges.try_emplace(Edge { parent, letter }, (uint32_t)nodes.size());
    if (inserted) {
        const Node& parentNode = nodes[parent];
        nodes.push_back({ .parent = parent,
            .letter = letter,
            .element = parentNode.element,
            .depth = parentNode.depth + 1 });
    }
    return it->second;
}

void MemberPrefixes::buildPath(Solver& solver, WordId w) {
    WordInfo& info = words[w.id()];
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

void MemberPrefixes::attach(Solver& solver, WordId w, bool raiseConflicts) {
    WordInfo& info = words[w.id()];
    VERIFY(info.occurrenceIndices.empty());

    if (info.isPath()) {
        // A path occupies every prefix of its normal form, so that a prefix candidate ending in any
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
                    solver.assignTrue(false_literal, makeReason<ReasonKind::MemberPrefixHit>({ .prefix = prefix, .path = w }));
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
                solver.assignTrue(false_literal, makeReason<ReasonKind::MemberPrefixHit>({ .prefix = w, .path = path }));
        }
    }
}

void MemberPrefixes::detach(WordId w) {
    WordInfo& info = words[w.id()];

    auto removeAt = [&](std::vector<WordId>& list, uint32_t index, uint32_t indexSlot) {
        VERIFY(list[index] == w);
        if (index + 1 != list.size()) {
            list[index] = list.back();
            words[list[index].id()].occurrenceIndices[indexSlot] = index;
        }
        list.pop_back();
    };

    if (info.isPath()) {
        VERIFY(info.occurrenceIndices.size() == info.path.size());
        for (int_t k = info.path.size() - 1; k >= 0; k--) {
            uint32_t node = info.path[k];
            // The depth of a node is unique, so it is also the index into the path of every other
            // path word occupying it.
            VERIFY(nodeOf(node).depth == (uint32_t)k);
            removeAt(nodeOf(node).aOccurrences, info.occurrenceIndices[k], k);
        }
    } else {
        VERIFY(info.occurrenceIndices.size() == 1);
        uint32_t node = info.path.back();
        removeAt(nodeOf(node).bTerminals, info.occurrenceIndices[0], 0);
    }

    info.occurrenceIndices.clear();
    info.path.clear();
}

MemberPrefixes::WordId MemberPrefixes::addWord(Solver& solver, ElementId element, Member expression, Sets::Containment payload) {
    while (element.id() >= elementRoots.size()) {
        uint32_t root = nodes.size();
        nodes.push_back({ .parent = NIL, .letter = (Member)INVALID_VALUE, .element = ElementId(elementRoots.size()), .depth = 0 });
        elementRoots.push_back(root);
    }

    WordId w(words.size());
    words.push_back({ .element = element, .expression = expression, .payload = payload });

    solver.addUse(expression, Use(UseKind::MemberPrefixWord, w.id()));

    buildPath(solver, w);
    attach(solver, w, true);
    return w;
}

bool MemberPrefixes::isPrefixOf(WordId prefix, WordId path) const {
    if (isBacktracked(prefix) || isBacktracked(path))
        return false;
    const WordInfo& prefixInfo = words[prefix.id()];
    const WordInfo& pathInfo = words[path.id()];
    VERIFY(prefixInfo.element == pathInfo.element);
    // Because a node is identified with the word it spells, comparing the single node at the
    // length of the prefix decides the whole prefix relation.
    if (pathInfo.path.size() < prefixInfo.path.size())
        return false;
    return pathInfo.path[prefixInfo.path.size() - 1] == prefixInfo.path.back();
}

void MemberPrefixes::explainPrefix(Solver& solver, WordId prefix, WordId path, ClauseBuilder& clause) {
    // The prefix relation follows from the normal forms of the two expressions alone, so the
    // equalities behind their rewrites are all that has to be justified.
    // TODO: Only the rewrites covering the matched prefix of the path are needed, explaining the
    //       whole expression is sound but produces a longer clause than necessary.
    auto& members = solver.impl().members;
    members.explainRewrite(solver, words[prefix.id()].expression, clause);
    members.explainRewrite(solver, words[path.id()].expression, clause);
}

void MemberPrefixes::propagateRewrite(Solver& solver, Use use) {
    // Only growing rewrites are notified about, so this never runs while the assignments are being
    // reverted and a conflict found here is a real one
    VERIFY(!backtracking());
    VERIFY(use.kind() == UseKind::MemberPrefixWord);
    WordId w = WordId(use.id());

    rebuiltTrace.push_back(w);
    detach(w);
    buildPath(solver, w);
    attach(solver, w, true);
}

void MemberPrefixes::newDecisionLevel(Solver& solver) {
    wordDecisionPoints.push_back(words.size());
    rebuiltDecisionPoints.push_back(rebuiltTrace.size());
    VERIFY((int_t)wordDecisionPoints.size() == solver.currentDecisionLevel() + 1);
    VERIFY((int_t)rebuiltDecisionPoints.size() == solver.currentDecisionLevel() + 1);
}

void MemberPrefixes::beginBacktrack(Solver& solver) {
    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
    int_t targetSize = wordDecisionPoints[lastLevelToRevert];
    backtrackedWordCount = targetSize;

    for (int_t i = (int_t)words.size() - 1; i >= targetSize; i--)
        detach(WordId(i));

    // Members reverted the rewrites of these levels before this was called, so the normal form a
    // word goes back to is recomputed from it, see \ref rebuilds. Recomputing is idempotent, so a
    // word recorded several times needs no special treatment.
    int_t rebuiltTargetSize = rebuiltDecisionPoints[lastLevelToRevert];
    while ((int_t)rebuiltTrace.size() > rebuiltTargetSize) {
        WordId w = rebuiltTrace.back();
        rebuiltTrace.pop_back();
        // The word was unregistered above, its normal form no longer matters
        if (isBacktracked(w))
            continue;

        detach(w);
        buildPath(solver, w);
        // Reverting a rewrite can only destroy prefix relations, see \ref monotonicity, so there is
        // nothing to raise here. Which is required, the assignments are still being reverted.
        attach(solver, w, false);
    }
    rebuiltDecisionPoints.resize(lastLevelToRevert);
}

void MemberPrefixes::endBacktrack(Solver& solver) {
    int_t lastLevelToRevert = solver.currentDecisionLevel() + 1;
    VERIFY(backtrackedWordCount == wordDecisionPoints[lastLevelToRevert]);
    backtrackedWordCount = limits::max;
    words.erase(words.begin() + wordDecisionPoints[lastLevelToRevert], words.end());
    wordDecisionPoints.resize(lastLevelToRevert);
}

void MemberPrefixes::checkInvariances(Solver& solver) {
    VERIFY(!backtracking());
    // The entries of a level are appended after its decision point, so the points only grow
    auto checkDecisionPoints = [&solver](const std::vector<uint32_t>& points, size_t traceSize) {
        VERIFY((int_t)points.size() == solver.currentDecisionLevel() + 1);
        VERIFY(std::ranges::is_sorted(points));
        VERIFY(points.empty() || points.back() <= traceSize);
    };
    checkDecisionPoints(wordDecisionPoints, words.size());
    checkDecisionPoints(rebuiltDecisionPoints, rebuiltTrace.size());

    // A word is rebuilt at the level it was registered at or above, so a rebuild is never left
    // behind by the backtrack that unregistered its word
    for (WordId w : rebuiltTrace)
        VERIFY(w.id() < words.size());

    for (uint32_t node = 0; node < nodes.size(); node++) {
        const Node& n = nodes[node];
        if (n.parent != NIL) {
            VERIFY(nodes[n.parent].element == n.element);
            VERIFY(nodes[n.parent].depth + 1 == n.depth);
            VERIFY(edges.at({ n.parent, n.letter }) == node);
        }

        for (uint32_t i = 0; i < n.aOccurrences.size(); i++) {
            const WordInfo& info = words[n.aOccurrences[i].id()];
            VERIFY(info.isPath());
            VERIFY(info.path[n.depth] == node);
            VERIFY(info.occurrenceIndices[n.depth] == i);
        }
        for (uint32_t i = 0; i < n.bTerminals.size(); i++) {
            const WordInfo& info = words[n.bTerminals[i].id()];
            VERIFY(!info.isPath());
            VERIFY(info.path.back() == node);
            VERIFY(info.occurrenceIndices[0] == i);
        }
    }

    for (uint32_t i = 0; i < words.size(); i++) {
        const WordInfo& info = words[i];
        // The path spells the normal form of the expression, so anything else here means that a
        // notification of the use registered for the word was missed
        std::vector<Member> normalForm;
        solver.impl().members.appendRewrite(info.expression, normalForm);
        VERIFY(info.path.size() == normalForm.size() + 1);
        VERIFY(info.path.front() == elementRoots[info.element.id()]);
        for (uint32_t k = 0; k < normalForm.size(); k++) {
            VERIFY(nodes[info.path[k + 1]].letter == normalForm[k]);
            VERIFY(nodes[info.path[k + 1]].parent == info.path[k]);
        }
    }
}

}
